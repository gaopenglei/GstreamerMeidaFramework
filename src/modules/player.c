/**
 * @file player.c
 * @brief 音视频播放模块实现
 * @details 基于 GStreamer 实现完整的音视频播放功能
 */

#include "player.h"
#include "logger.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief 播放器结构体定义
 */
struct MediaPlayer {
    PlayerConfig config;            /**< 配置 */
    PlayerCallbacks callbacks;      /**< 回调 */
    
    GstElement *pipeline;           /**< GStreamer 管道 */
    GstElement *playbin;            /**< playbin 元素 */
    GstElement *video_sink;         /**< 视频输出 */
    GstElement *audio_sink;         /**< 音频输出 */
    
    GstBus *bus;                    /**< 消息总线 */
    guint bus_watch_id;             /**< 总线监听ID */
    
    MediaState state;               /**< 当前状态 */
    MediaInfo media_info;           /**< 媒体信息 */
    
    pthread_mutex_t mutex;          /**< 互斥锁 */
    pthread_t thread;               /**< 消息处理线程 */
    int running;                    /**< 运行标志 */
    
    int64_t position;               /**< 当前位置 */
    int64_t duration;               /**< 时长 */
    
    double volume;                  /**< 音量 */
    int mute;                       /**< 静音 */
    double rate;                    /**< 播放速度 */
    
    uintptr_t window_id;            /**< 窗口ID */
};

/* 前向声明 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data);
static void parse_stream_info(MediaPlayer *player);

/**
 * @brief 初始化播放器配置为默认值
 */
void player_config_init(PlayerConfig *config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(PlayerConfig));
    strcpy(config->video_sink, "autovideosink");
    strcpy(config->audio_sink, "autoaudiosink");
    config->low_latency = 1;
    config->buffer_duration = 200;  /* 200ms */
    config->buffer_size = 1024 * 1024;  /* 1MB */
    config->enable_hardware_decode = 1;
    config->sync = 1;
    config->position_update_interval = 100;  /* 100ms */
}

/**
 * @brief 初始化播放器回调为默认值
 */
void player_callbacks_init(PlayerCallbacks *callbacks) {
    if (callbacks == NULL) {
        return;
    }
    
    memset(callbacks, 0, sizeof(PlayerCallbacks));
}

/**
 * @brief GStreamer 消息总线回调
 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    (void)bus;
    MediaPlayer *player = (MediaPlayer *)data;
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &err, &debug);
            
            LOG_ERROR("GStreamer error: %s (%s)", err->message, debug);
            
            if (player->callbacks.on_error) {
                player->callbacks.on_error(player, MEDIA_ERROR_GST_MESSAGE_ERROR,
                                          err->message, player->callbacks.user_data);
            }
            
            g_error_free(err);
            g_free(debug);
            
            player->state = MEDIA_STATE_ERROR;
            if (player->callbacks.on_state_changed) {
                player->callbacks.on_state_changed(player, MEDIA_STATE_ERROR,
                                                   player->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_EOS: {
            LOG_INFO("End of stream reached");
            player->state = MEDIA_STATE_EOS;
            
            if (player->callbacks.on_event) {
                MediaEvent event = {
                    .type = MEDIA_EVENT_EOS,
                    .code = 0,
                    .message = "End of stream"
                };
                player->callbacks.on_event(&event, player->callbacks.user_data);
            }
            
            if (player->callbacks.on_state_changed) {
                player->callbacks.on_state_changed(player, MEDIA_STATE_EOS,
                                                   player->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(player->playbin)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
                (void)pending_state;
                
                LOG_DEBUG("State changed: %s -> %s",
                         gst_element_state_get_name(old_state),
                         gst_element_state_get_name(new_state));
                
                MediaState media_state = MEDIA_STATE_NULL;
                switch (new_state) {
                    case GST_STATE_NULL:
                        media_state = MEDIA_STATE_NULL;
                        break;
                    case GST_STATE_READY:
                        media_state = MEDIA_STATE_READY;
                        break;
                    case GST_STATE_PAUSED:
                        media_state = MEDIA_STATE_PAUSED;
                        break;
                    case GST_STATE_PLAYING:
                        media_state = MEDIA_STATE_PLAYING;
                        break;
                    default:
                        break;
                }
                
                player->state = media_state;
                if (player->callbacks.on_state_changed) {
                    player->callbacks.on_state_changed(player, media_state,
                                                       player->callbacks.user_data);
                }
            }
            break;
        }
        
        case GST_MESSAGE_DURATION_CHANGED: {
            gst_element_query_duration(player->playbin, GST_FORMAT_TIME,
                                       &player->duration);
            LOG_DEBUG("Duration changed: %" GST_TIME_FORMAT,
                     GST_TIME_ARGS(player->duration));
            break;
        }
        
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(message, &percent);
            LOG_DEBUG("Buffering: %d%%", percent);
            
            if (player->callbacks.on_event) {
                MediaEvent event = {
                    .type = MEDIA_EVENT_BUFFERING,
                    .code = percent
                };
                snprintf(event.message, sizeof(event.message), "Buffering: %d%%", percent);
                player->callbacks.on_event(&event, player->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_STREAM_START: {
            LOG_INFO("Stream started");
            parse_stream_info(player);
            
            if (player->callbacks.on_event) {
                MediaEvent event = {
                    .type = MEDIA_EVENT_STREAM_START,
                    .message = "Stream started"
                };
                player->callbacks.on_event(&event, player->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_TAG: {
            GstTagList *tags = NULL;
            gst_message_parse_tag(message, &tags);
            
            if (player->callbacks.on_event) {
                MediaEvent event = {
                    .type = MEDIA_EVENT_TAG,
                    .data = tags
                };
                player->callbacks.on_event(&event, player->callbacks.user_data);
            }
            
            gst_tag_list_unref(tags);
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}

/**
 * @brief 解析流信息
 */
static void parse_stream_info(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return;
    }
    
    gint n_video, n_audio;
    g_object_get(player->playbin, "n-video", &n_video, "n-audio", &n_audio, NULL);
    
    player->media_info.has_video = (n_video > 0);
    player->media_info.has_audio = (n_audio > 0);
    
    /* 获取视频信息 */
    if (n_video > 0) {
        GstPad *video_pad = NULL;
        g_signal_emit_by_name(player->playbin, "get-video-pad", 0, &video_pad);
        
        if (video_pad != NULL) {
            GstCaps *caps = gst_pad_get_current_caps(video_pad);
            if (caps != NULL) {
                GstStructure *str = gst_caps_get_structure(caps, 0);
                
                gint width, height, fps_num, fps_den;
                gst_structure_get_int(str, "width", &width);
                gst_structure_get_int(str, "height", &height);
                gst_structure_get_fraction(str, "framerate", &fps_num, &fps_den);
                
                player->media_info.video_params.width = width;
                player->media_info.video_params.height = height;
                player->media_info.video_params.framerate_num = fps_num;
                player->media_info.video_params.framerate_den = fps_den;
                
                gst_caps_unref(caps);
            }
            gst_object_unref(video_pad);
        }
    }
    
    /* 获取音频信息 */
    if (n_audio > 0) {
        GstPad *audio_pad = NULL;
        g_signal_emit_by_name(player->playbin, "get-audio-pad", 0, &audio_pad);
        
        if (audio_pad != NULL) {
            GstCaps *caps = gst_pad_get_current_caps(audio_pad);
            if (caps != NULL) {
                GstStructure *str = gst_caps_get_structure(caps, 0);
                
                gint rate, channels;
                gst_structure_get_int(str, "rate", &rate);
                gst_structure_get_int(str, "channels", &channels);
                
                player->media_info.audio_params.sample_rate = rate;
                player->media_info.audio_params.channels = channels;
                
                gst_caps_unref(caps);
            }
            gst_object_unref(audio_pad);
        }
    }
    
    /* 获取时长 */
    gst_element_query_duration(player->playbin, GST_FORMAT_TIME, &player->duration);
    player->media_info.duration = player->duration;
    
    LOG_INFO("Media info: video=%s, audio=%s, duration=%" GST_TIME_FORMAT,
             player->media_info.has_video ? "yes" : "no",
             player->media_info.has_audio ? "yes" : "no",
             GST_TIME_ARGS(player->duration));
}

/**
 * @brief 创建播放器
 */
MediaPlayer *player_create(const PlayerConfig *config) {
    gst_init(NULL, NULL);

    MediaPlayer *player = (MediaPlayer *)malloc(sizeof(MediaPlayer));
    if (player == NULL) {
        LOG_ERROR("Failed to allocate memory for player");
        return NULL;
    }
    
    memset(player, 0, sizeof(MediaPlayer));
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&player->config, config, sizeof(PlayerConfig));
    } else {
        player_config_init(&player->config);
    }
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&player->mutex, NULL);
    
    /* 初始化状态 */
    player->state = MEDIA_STATE_NULL;
    player->volume = 1.0;
    player->mute = 0;
    player->rate = 1.0;
    
    /* 初始化媒体信息 */
    media_info_init(&player->media_info);
    
    LOG_INFO("Player created successfully");
    return player;
}

/**
 * @brief 销毁播放器
 */
void player_destroy(MediaPlayer *player) {
    if (player == NULL) {
        return;
    }
    
    LOG_INFO("Destroying player");
    
    /* 停止播放 */
    player_stop(player);
    
    /* 关闭媒体 */
    player_close(player);
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&player->mutex);
    
    free(player);
    LOG_INFO("Player destroyed");
}

/**
 * @brief 设置播放器回调
 */
MediaErrorCode player_set_callbacks(MediaPlayer *player, const PlayerCallbacks *callbacks) {
    if (player == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (callbacks != NULL) {
        memcpy(&player->callbacks, callbacks, sizeof(PlayerCallbacks));
    } else {
        player_callbacks_init(&player->callbacks);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 打开媒体文件
 */
MediaErrorCode player_open(MediaPlayer *player, const char *uri) {
    if (player == NULL || uri == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    LOG_INFO("Opening media: %s", uri);
    
    /* 创建 playbin */
    player->playbin = gst_element_factory_make("playbin", "playbin");
    if (player->playbin == NULL) {
        LOG_ERROR("Failed to create playbin element");
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_ERROR_GST_ELEMENT_CREATE;
    }
    
    /* 设置 URI */
    gchar *real_uri = NULL;
    if (strstr(uri, "://") != NULL) {
        real_uri = g_strdup(uri);
    } else {
        real_uri = gst_filename_to_uri(uri, NULL);
    }
    
    g_object_set(player->playbin, "uri", real_uri, NULL);
    g_object_set(player->playbin, "volume", player->volume, "mute",
                 player->mute ? TRUE : FALSE, NULL);
    strncpy(player->media_info.uri, real_uri, sizeof(player->media_info.uri) - 1);
    g_free(real_uri);
    
    /* 创建视频输出 */
    player->video_sink = gst_element_factory_make(player->config.video_sink, "video_sink");
    if (player->video_sink != NULL) {
        g_object_set(player->playbin, "video-sink", player->video_sink, NULL);
        
        /* 设置低延迟模式 */
        if (player->config.low_latency) {
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(player->video_sink), "sync")) {
                g_object_set(player->video_sink, "sync", player->config.sync, NULL);
            }
        }
    }
    
    /* 创建音频输出 */
    player->audio_sink = gst_element_factory_make(player->config.audio_sink, "audio_sink");
    if (player->audio_sink != NULL) {
        g_object_set(player->playbin, "audio-sink", player->audio_sink, NULL);
    }
    
    /* 设置缓冲参数 */
    if (player->config.buffer_duration > 0) {
        g_object_set(player->playbin, 
                    "buffer-duration", player->config.buffer_duration * GST_MSECOND,
                    "buffer-size", player->config.buffer_size,
                    NULL);
    }
    
    /* 设置消息总线 */
    player->bus = gst_pipeline_get_bus(GST_PIPELINE(player->playbin));
    player->bus_watch_id = gst_bus_add_watch(player->bus, bus_callback, player);
    gst_object_unref(player->bus);
    
    /* 设置就绪状态 */
    GstStateChangeReturn ret = gst_element_set_state(player->playbin, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to set pipeline to READY state");
        gst_object_unref(player->playbin);
        player->playbin = NULL;
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    player->state = MEDIA_STATE_READY;
    
    pthread_mutex_unlock(&player->mutex);
    
    LOG_INFO("Media opened successfully");
    return MEDIA_OK;
}

MediaErrorCode player_set_uri(MediaPlayer *player, const char *uri) {
    if (player == NULL || uri == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }

    MediaErrorCode ret = player_close(player);
    if (ret != MEDIA_OK) {
        return ret;
    }

    return player_open(player, uri);
}

/**
 * @brief 关闭媒体文件
 */
MediaErrorCode player_close(MediaPlayer *player) {
    if (player == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    if (player->playbin == NULL) {
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_OK;
    }
    
    LOG_INFO("Closing media");
    
    /* 停止管道 */
    gst_element_set_state(player->playbin, GST_STATE_NULL);
    
    /* 移除总线监听 */
    if (player->bus_watch_id > 0) {
        g_source_remove(player->bus_watch_id);
        player->bus_watch_id = 0;
    }
    
    /* 释放资源 */
    gst_object_unref(player->playbin);
    player->playbin = NULL;
    player->video_sink = NULL;
    player->audio_sink = NULL;
    
    /* 重置状态 */
    player->state = MEDIA_STATE_NULL;
    player->position = 0;
    player->duration = 0;
    media_info_init(&player->media_info);
    
    pthread_mutex_unlock(&player->mutex);
    
    LOG_INFO("Media closed");
    return MEDIA_OK;
}

/**
 * @brief 开始播放
 */
MediaErrorCode player_play(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    LOG_INFO("Starting playback");
    
    GstStateChangeReturn ret = gst_element_set_state(player->playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to start playback");
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    player->state = MEDIA_STATE_PLAYING;
    
    pthread_mutex_unlock(&player->mutex);
    return MEDIA_OK;
}

/**
 * @brief 暂停播放
 */
MediaErrorCode player_pause(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    LOG_INFO("Pausing playback");
    
    GstStateChangeReturn ret = gst_element_set_state(player->playbin, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to pause playback");
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    player->state = MEDIA_STATE_PAUSED;
    
    pthread_mutex_unlock(&player->mutex);
    return MEDIA_OK;
}

/**
 * @brief 停止播放
 */
MediaErrorCode player_stop(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    LOG_INFO("Stopping playback");
    
    gst_element_set_state(player->playbin, GST_STATE_READY);
    player->state = MEDIA_STATE_READY;
    player->position = 0;
    
    pthread_mutex_unlock(&player->mutex);
    return MEDIA_OK;
}

/**
 * @brief 定位播放
 */
MediaErrorCode player_seek(MediaPlayer *player, int64_t position) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&player->mutex);
    
    LOG_DEBUG("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS(position));
    
    gboolean ret = gst_element_seek_simple(player->playbin,
                                           GST_FORMAT_TIME,
                                           GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                           position);
    
    if (!ret) {
        LOG_ERROR("Seek failed");
        pthread_mutex_unlock(&player->mutex);
        return MEDIA_ERROR_IO_SEEK_FAILED;
    }
    
    player->position = position;
    
    pthread_mutex_unlock(&player->mutex);
    return MEDIA_OK;
}

/**
 * @brief 获取当前播放位置
 */
int64_t player_get_position(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return 0;
    }
    
    gint64 position = 0;
    gst_element_query_position(player->playbin, GST_FORMAT_TIME, &position);
    
    player->position = position;
    return position;
}

/**
 * @brief 获取媒体时长
 */
int64_t player_get_duration(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) {
        return 0;
    }
    
    gint64 duration = 0;
    gst_element_query_duration(player->playbin, GST_FORMAT_TIME, &duration);
    
    player->duration = duration;
    return duration;
}

/**
 * @brief 获取当前播放状态
 */
MediaState player_get_state(MediaPlayer *player) {
    if (player == NULL) {
        return MEDIA_STATE_NULL;
    }
    return player->state;
}

/**
 * @brief 获取媒体信息
 */
MediaErrorCode player_get_media_info(MediaPlayer *player, MediaInfo *info) {
    if (player == NULL || info == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    memcpy(info, &player->media_info, sizeof(MediaInfo));
    info->position = player_get_position(player);
    
    return MEDIA_OK;
}

/**
 * @brief 设置音量
 */
MediaErrorCode player_set_volume(MediaPlayer *player, double volume) {
    if (player == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (volume < 0.0) volume = 0.0;
    if (volume > 1.0) volume = 1.0;
    
    if (player->playbin != NULL) {
        g_object_set(player->playbin, "volume", volume, NULL);
    }
    player->volume = volume;
    
    return MEDIA_OK;
}

/**
 * @brief 获取音量
 */
double player_get_volume(MediaPlayer *player) {
    if (player == NULL) {
        return 0.0;
    }

    if (player->playbin != NULL) {
        gdouble volume;
        g_object_get(player->playbin, "volume", &volume, NULL);
        player->volume = volume;
    }
    
    return player->volume;
}

/**
 * @brief 设置静音
 */
MediaErrorCode player_set_mute(MediaPlayer *player, int mute) {
    if (player == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }

    player->mute = mute ? 1 : 0;
    if (player->playbin != NULL) {
        g_object_set(player->playbin, "mute", player->mute ? TRUE : FALSE, NULL);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 获取静音状态
 */
int player_get_mute(MediaPlayer *player) {
    if (player == NULL) {
        return 0;
    }

    if (player->playbin != NULL) {
        gboolean mute;
        g_object_get(player->playbin, "mute", &mute, NULL);
        player->mute = mute;
    }
    
    return player->mute;
}

/**
 * @brief 设置播放速度
 */
MediaErrorCode player_set_rate(MediaPlayer *player, double rate) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (rate <= 0.0) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    gint64 position = player_get_position(player);
    
    gboolean ret = gst_element_seek(player->playbin, rate,
                                    GST_FORMAT_TIME,
                                    GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                                    GST_SEEK_TYPE_SET, position,
                                    GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    
    if (!ret) {
        LOG_ERROR("Failed to set playback rate");
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    player->rate = rate;
    return MEDIA_OK;
}

/**
 * @brief 获取播放速度
 */
double player_get_rate(MediaPlayer *player) {
    if (player == NULL) {
        return 1.0;
    }
    return player->rate;
}

/**
 * @brief 设置视频输出窗口
 */
MediaErrorCode player_set_window(MediaPlayer *player, uintptr_t window_id) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    player->window_id = window_id;
    
    if (player->video_sink != NULL && window_id != 0) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(player->video_sink), 
                                            window_id);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 设置视频渲染区域
 */
MediaErrorCode player_set_render_rect(MediaPlayer *player, const Rect *rect) {
    if (player == NULL || player->video_sink == NULL || rect == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(player->video_sink),
                                           rect->x, rect->y, rect->width, rect->height);
    
    return MEDIA_OK;
}

/**
 * @brief 获取当前视频帧
 */
MediaErrorCode player_get_frame(MediaPlayer *player, uint8_t *buffer, 
                                int width, int height) {
    (void)width;
    (void)height;
    if (player == NULL || player->playbin == NULL || buffer == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    /* 使用 appsink 获取当前帧 - 需要在管道中配置 */
    LOG_WARN("player_get_frame requires appsink configuration");
    return MEDIA_ERROR_NOT_SUPPORTED;
}
