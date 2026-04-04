/**
 * @file transcoder.c
 * @brief 音视频转码模块实现
 * @details 基于 GStreamer 实现完整的音视频转码功能
 */

#include "transcoder.h"
#include "logger.h"
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief 转码器结构体定义
 */
struct MediaTranscoder {
    TranscoderConfig config;            /**< 配置 */
    TranscoderCallbacks callbacks;      /**< 回调 */
    
    GstElement *pipeline;               /**< GStreamer 管道 */
    GstElement *uridecodebin;           /**< 解码器 */
    GstElement *video_queue;            /**< 视频队列 */
    GstElement *video_enc;              /**< 视频编码器 */
    GstElement *audio_queue;            /**< 音频队列 */
    GstElement *audio_enc;              /**< 音频编码器 */
    GstElement *muxer;                  /**< 复用器 */
    GstElement *sink;                   /**< 输出 */
    
    GstBus *bus;                        /**< 消息总线 */
    guint bus_watch_id;                 /**< 总线监听ID */
    guint progress_id;                  /**< 进度更新ID */
    
    MediaState state;                   /**< 当前状态 */
    MediaInfo input_info;               /**< 输入媒体信息 */
    TranscodeProgress progress;         /**< 转码进度 */
    
    pthread_mutex_t mutex;              /**< 互斥锁 */
    
    int64_t start_time;                 /**< 开始时间 */
    int64_t last_position;              /**< 上次位置 */
};

/* 前向声明 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data);
static gboolean progress_callback(gpointer data);
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer data);
static GstElement *create_video_encoder(VideoCodec codec, const VideoParams *params);
static GstElement *create_audio_encoder(AudioCodec codec, const AudioParams *params);
static GstElement *create_muxer(ContainerFormat format);

/**
 * @brief 初始化转码器配置为默认值
 */
void transcoder_config_init(TranscoderConfig *config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(TranscoderConfig));
    
    /* 默认输出配置 */
    config->output_container = CONTAINER_MP4;
    
    /* 默认视频转码配置 */
    config->video_transcode = 1;
    video_params_init(&config->video_params);
    config->video_params.codec = VIDEO_CODEC_H265;
    config->video_params.bitrate = 4000000;
    
    /* 默认音频转码配置 */
    config->audio_transcode = 1;
    audio_params_init(&config->audio_params);
    config->audio_params.codec = AUDIO_CODEC_AAC;
    config->audio_params.bitrate = 128000;
    
    /* 性能配置 */
    config->low_latency = 1;
    config->enable_hardware = 1;
    config->threads = 4;
    config->copy_ts = 1;
    
    /* 转码范围 */
    config->start_time = 0;
    config->end_time = -1;  /* -1 表示到文件结束 */
}

/**
 * @brief 初始化转码器回调为默认值
 */
void transcoder_callbacks_init(TranscoderCallbacks *callbacks) {
    if (callbacks == NULL) {
        return;
    }
    
    memset(callbacks, 0, sizeof(TranscoderCallbacks));
}

/**
 * @brief 创建视频编码器
 */
static GstElement *create_video_encoder(VideoCodec codec, const VideoParams *params) {
    GstElement *encoder = NULL;
    gchar *encoder_name = g_strdup_printf("video_enc_%s", video_codec_to_string(codec));
    
    switch (codec) {
        case VIDEO_CODEC_H264:
            /* 尝试硬件编码器 */
            encoder = gst_element_factory_make("nvh264enc", encoder_name);
            if (encoder == NULL) {
                encoder = gst_element_factory_make("vaapih264enc", encoder_name);
            }
            if (encoder == NULL) {
                encoder = gst_element_factory_make("x264enc", encoder_name);
            }
            if (encoder != NULL) {
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "bitrate")) {
                    g_object_set(encoder, "bitrate", params->bitrate / 1000, NULL);
                }
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "key-int-max")) {
                    g_object_set(encoder, "key-int-max", params->gop_size, NULL);
                }
            }
            break;
            
        case VIDEO_CODEC_H265:
            encoder = gst_element_factory_make("nvh265enc", encoder_name);
            if (encoder == NULL) {
                encoder = gst_element_factory_make("vaapih265enc", encoder_name);
            }
            if (encoder == NULL) {
                encoder = gst_element_factory_make("x265enc", encoder_name);
            }
            if (encoder != NULL) {
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "bitrate")) {
                    g_object_set(encoder, "bitrate", params->bitrate / 1000, NULL);
                }
            }
            break;
            
        case VIDEO_CODEC_VP8:
            encoder = gst_element_factory_make("vp8enc", encoder_name);
            if (encoder != NULL) {
                g_object_set(encoder, "target-bitrate", params->bitrate, NULL);
            }
            break;
            
        case VIDEO_CODEC_VP9:
            encoder = gst_element_factory_make("vp9enc", encoder_name);
            break;
            
        default:
            LOG_WARN("Unsupported video codec: %s", video_codec_to_string(codec));
            break;
    }
    
    g_free(encoder_name);
    return encoder;
}

/**
 * @brief 创建音频编码器
 */
static GstElement *create_audio_encoder(AudioCodec codec, const AudioParams *params) {
    GstElement *encoder = NULL;
    gchar *encoder_name = g_strdup_printf("audio_enc_%s", audio_codec_to_string(codec));
    
    switch (codec) {
        case AUDIO_CODEC_AAC:
            encoder = gst_element_factory_make("avenc_aac", encoder_name);
            if (encoder == NULL) {
                encoder = gst_element_factory_make("faac", encoder_name);
            }
            if (encoder != NULL) {
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "bitrate")) {
                    g_object_set(encoder, "bitrate", params->bitrate, NULL);
                }
            }
            break;
            
        case AUDIO_CODEC_OPUS:
            encoder = gst_element_factory_make("opusenc", encoder_name);
            if (encoder != NULL) {
                g_object_set(encoder, "bitrate", params->bitrate, NULL);
            }
            break;
            
        case AUDIO_CODEC_MP3:
            encoder = gst_element_factory_make("lamemp3enc", encoder_name);
            if (encoder != NULL) {
                g_object_set(encoder, "bitrate", params->bitrate / 1000, NULL);
            }
            break;
            
        case AUDIO_CODEC_VORBIS:
            encoder = gst_element_factory_make("vorbisenc", encoder_name);
            if (encoder != NULL) {
                g_object_set(encoder, "bitrate", params->bitrate, NULL);
            }
            break;
            
        default:
            LOG_WARN("Unsupported audio codec: %s", audio_codec_to_string(codec));
            break;
    }
    
    g_free(encoder_name);
    return encoder;
}

/**
 * @brief 创建复用器
 */
static GstElement *create_muxer(ContainerFormat format) {
    GstElement *muxer = NULL;
    
    switch (format) {
        case CONTAINER_MP4:
            muxer = gst_element_factory_make("mp4mux", "muxer");
            break;
            
        case CONTAINER_MKV:
            muxer = gst_element_factory_make("matroskamux", "muxer");
            break;
            
        case CONTAINER_WEBM:
            muxer = gst_element_factory_make("webmmux", "muxer");
            break;
            
        case CONTAINER_TS:
            muxer = gst_element_factory_make("mpegtsmux", "muxer");
            break;
            
        default:
            muxer = gst_element_factory_make("mp4mux", "muxer");
            break;
    }
    
    return muxer;
}

/**
 * @brief GStreamer 消息总线回调
 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    MediaTranscoder *transcoder = (MediaTranscoder *)data;
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &err, &debug);
            
            LOG_ERROR("Transcoder error: %s (%s)", err->message, debug);
            
            if (transcoder->callbacks.on_error) {
                transcoder->callbacks.on_error(transcoder, MEDIA_ERROR_GST_MESSAGE_ERROR,
                                               err->message, transcoder->callbacks.user_data);
            }
            
            g_error_free(err);
            g_free(debug);
            
            transcoder->state = MEDIA_STATE_ERROR;
            if (transcoder->callbacks.on_state_changed) {
                transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_ERROR,
                                                       transcoder->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_EOS: {
            LOG_INFO("Transcoding completed");
            transcoder->state = MEDIA_STATE_READY;
            transcoder->progress.progress = 100.0;
            
            if (transcoder->callbacks.on_progress) {
                transcoder->callbacks.on_progress(transcoder, &transcoder->progress,
                                                  transcoder->callbacks.user_data);
            }
            
            if (transcoder->callbacks.on_state_changed) {
                transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_READY,
                                                       transcoder->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(transcoder->pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
                
                LOG_DEBUG("Transcoder state changed: %s -> %s",
                         gst_element_state_get_name(old_state),
                         gst_element_state_get_name(new_state));
            }
            break;
        }
        
        case GST_MESSAGE_DURATION_CHANGED: {
            /* 更新时长信息 */
            gint64 duration;
            if (gst_element_query_duration(transcoder->pipeline, GST_FORMAT_TIME, &duration)) {
                transcoder->progress.duration = duration;
                transcoder->input_info.duration = duration;
            }
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}

/**
 * @brief 进度更新回调
 */
static gboolean progress_callback(gpointer data) {
    MediaTranscoder *transcoder = (MediaTranscoder *)data;
    
    if (transcoder->state != MEDIA_STATE_PLAYING) {
        return TRUE;
    }
    
    /* 查询当前位置 */
    gint64 position;
    if (gst_element_query_position(transcoder->pipeline, GST_FORMAT_TIME, &position)) {
        transcoder->progress.position = position;
        
        /* 计算进度百分比 */
        if (transcoder->progress.duration > 0) {
            transcoder->progress.progress = 
                (double)position / transcoder->progress.duration * 100.0;
        }
        
        /* 计算转码速度 */
        int64_t now = g_get_monotonic_time();
        int64_t elapsed = (now - transcoder->start_time) / 1000;  /* 毫秒 */
        transcoder->progress.elapsed_time = elapsed;
        
        if (elapsed > 0 && position > transcoder->last_position) {
            double speed = (double)(position - transcoder->last_position) / 
                          (double)(now - transcoder->last_position) * 1000000.0;
            transcoder->progress.speed = speed;
            
            /* 预计剩余时间 */
            if (transcoder->progress.progress > 0) {
                int64_t total_estimated = (int64_t)(elapsed / transcoder->progress.progress * 100.0);
                transcoder->progress.estimated_remaining = total_estimated - elapsed;
            }
        }
        
        transcoder->last_position = position;
        
        /* 调用进度回调 */
        if (transcoder->callbacks.on_progress) {
            transcoder->callbacks.on_progress(transcoder, &transcoder->progress,
                                              transcoder->callbacks.user_data);
        }
    }
    
    return TRUE;
}

/**
 * @brief pad 添加处理器
 */
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer data) {
    MediaTranscoder *transcoder = (MediaTranscoder *)data;
    GstCaps *caps = gst_pad_get_current_caps(new_pad);
    GstStructure *str;
    const gchar *name;
    
    if (caps == NULL) {
        caps = gst_pad_query_caps(new_pad, NULL);
    }
    
    str = gst_caps_get_structure(caps, 0);
    name = gst_structure_get_name(str);
    
    LOG_DEBUG("Pad added: %s", name);
    
    if (g_str_has_prefix(name, "video/")) {
        /* 链接视频编码器 */
        if (transcoder->video_enc != NULL) {
            GstPad *sink_pad = gst_element_get_static_pad(transcoder->video_queue, "sink");
            if (gst_pad_is_linked(sink_pad)) {
                gst_object_unref(sink_pad);
            } else if (gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
                LOG_ERROR("Failed to link video pad");
            } else {
                LOG_INFO("Video pad linked successfully");
            }
        }
    } else if (g_str_has_prefix(name, "audio/")) {
        /* 链接音频编码器 */
        if (transcoder->audio_enc != NULL) {
            GstPad *sink_pad = gst_element_get_static_pad(transcoder->audio_queue, "sink");
            if (gst_pad_is_linked(sink_pad)) {
                gst_object_unref(sink_pad);
            } else if (gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
                LOG_ERROR("Failed to link audio pad");
            } else {
                LOG_INFO("Audio pad linked successfully");
            }
        }
    }
    
    gst_caps_unref(caps);
}

/**
 * @brief 创建转码器
 */
MediaTranscoder *transcoder_create(const TranscoderConfig *config) {
    MediaTranscoder *transcoder = (MediaTranscoder *)malloc(sizeof(MediaTranscoder));
    if (transcoder == NULL) {
        LOG_ERROR("Failed to allocate memory for transcoder");
        return NULL;
    }
    
    memset(transcoder, 0, sizeof(MediaTranscoder));
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&transcoder->config, config, sizeof(TranscoderConfig));
    } else {
        transcoder_config_init(&transcoder->config);
    }
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&transcoder->mutex, NULL);
    
    /* 初始化状态 */
    transcoder->state = MEDIA_STATE_NULL;
    
    LOG_INFO("Transcoder created successfully");
    return transcoder;
}

/**
 * @brief 销毁转码器
 */
void transcoder_destroy(MediaTranscoder *transcoder) {
    if (transcoder == NULL) {
        return;
    }
    
    LOG_INFO("Destroying transcoder");
    
    /* 停止转码 */
    if (transcoder->state == MEDIA_STATE_PLAYING) {
        transcoder_stop(transcoder);
    }
    
    /* 销毁管道 */
    if (transcoder->pipeline != NULL) {
        gst_element_set_state(transcoder->pipeline, GST_STATE_NULL);
        
        if (transcoder->bus_watch_id > 0) {
            g_source_remove(transcoder->bus_watch_id);
        }
        if (transcoder->progress_id > 0) {
            g_source_remove(transcoder->progress_id);
        }
        
        gst_object_unref(transcoder->pipeline);
    }
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&transcoder->mutex);
    
    free(transcoder);
    LOG_INFO("Transcoder destroyed");
}

/**
 * @brief 设置转码器回调
 */
MediaErrorCode transcoder_set_callbacks(MediaTranscoder *transcoder, const TranscoderCallbacks *callbacks) {
    if (transcoder == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (callbacks != NULL) {
        memcpy(&transcoder->callbacks, callbacks, sizeof(TranscoderCallbacks));
    } else {
        transcoder_callbacks_init(&transcoder->callbacks);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 构建转码管道
 */
static MediaErrorCode build_pipeline(MediaTranscoder *transcoder) {
    GstElement *pipeline, *video_convert, *audio_convert;
    GstElement *video_queue2, *audio_queue2;
    
    /* 创建管道 */
    pipeline = gst_pipeline_new("transcoder_pipeline");
    if (pipeline == NULL) {
        LOG_ERROR("Failed to create pipeline");
        return MEDIA_ERROR_TRANSCODER_CREATE_PIPELINE;
    }
    
    /* 创建解码器 */
    transcoder->uridecodebin = gst_element_factory_make("uridecodebin", "decoder");
    if (transcoder->uridecodebin == NULL) {
        LOG_ERROR("Failed to create uridecodebin");
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_CREATE;
    }
    
    gchar *uri = gst_filename_to_uri(transcoder->config.input_file, NULL);
    g_object_set(transcoder->uridecodebin, "uri", uri, NULL);
    g_free(uri);
    
    /* 创建视频处理分支 */
    if (transcoder->config.video_transcode) {
        transcoder->video_queue = gst_element_factory_make("queue", "video_queue");
        video_convert = gst_element_factory_make("videoconvert", "video_convert");
        video_queue2 = gst_element_factory_make("queue", "video_queue2");
        transcoder->video_enc = create_video_encoder(transcoder->config.video_params.codec,
                                                     &transcoder->config.video_params);
        
        if (transcoder->video_enc == NULL) {
            LOG_ERROR("Failed to create video encoder");
            gst_object_unref(pipeline);
            return MEDIA_ERROR_TRANSCODER_NO_ENCODER;
        }
    }
    
    /* 创建音频处理分支 */
    if (transcoder->config.audio_transcode) {
        transcoder->audio_queue = gst_element_factory_make("queue", "audio_queue");
        audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
        audio_queue2 = gst_element_factory_make("queue", "audio_queue2");
        transcoder->audio_enc = create_audio_encoder(transcoder->config.audio_params.codec,
                                                     &transcoder->config.audio_params);
        
        if (transcoder->audio_enc == NULL) {
            LOG_ERROR("Failed to create audio encoder");
            gst_object_unref(pipeline);
            return MEDIA_ERROR_TRANSCODER_NO_ENCODER;
        }
    }
    
    /* 创建复用器和输出 */
    transcoder->muxer = create_muxer(transcoder->config.output_container);
    transcoder->sink = gst_element_factory_make("filesink", "sink");
    if (transcoder->sink == NULL) {
        LOG_ERROR("Failed to create filesink");
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_CREATE;
    }
    g_object_set(transcoder->sink, "location", transcoder->config.output_file, NULL);
    
    /* 添加元素到管道 */
    gst_bin_add(GST_BIN(pipeline), transcoder->uridecodebin);
    
    if (transcoder->config.video_transcode) {
        gst_bin_add_many(GST_BIN(pipeline),
                        transcoder->video_queue, video_convert, video_queue2,
                        transcoder->video_enc, NULL);
        
        /* 链接视频分支 */
        if (!gst_element_link_many(transcoder->video_queue, video_convert, video_queue2,
                                   transcoder->video_enc, NULL)) {
            LOG_ERROR("Failed to link video elements");
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
        
        /* 链接视频编码器到复用器 */
        GstPad *video_pad = gst_element_get_request_pad(transcoder->muxer, "video_%u");
        GstPad *enc_src = gst_element_get_static_pad(transcoder->video_enc, "src");
        if (gst_pad_link(enc_src, video_pad) != GST_PAD_LINK_OK) {
            LOG_ERROR("Failed to link video encoder to muxer");
            gst_object_unref(video_pad);
            gst_object_unref(enc_src);
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
        gst_object_unref(video_pad);
        gst_object_unref(enc_src);
    }
    
    if (transcoder->config.audio_transcode) {
        gst_bin_add_many(GST_BIN(pipeline),
                        transcoder->audio_queue, audio_convert, audio_queue2,
                        transcoder->audio_enc, NULL);
        
        /* 链接音频分支 */
        if (!gst_element_link_many(transcoder->audio_queue, audio_convert, audio_queue2,
                                   transcoder->audio_enc, NULL)) {
            LOG_ERROR("Failed to link audio elements");
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
        
        /* 链接音频编码器到复用器 */
        GstPad *audio_pad = gst_element_get_request_pad(transcoder->muxer, "audio_%u");
        GstPad *enc_src = gst_element_get_static_pad(transcoder->audio_enc, "src");
        if (gst_pad_link(enc_src, audio_pad) != GST_PAD_LINK_OK) {
            LOG_ERROR("Failed to link audio encoder to muxer");
            gst_object_unref(audio_pad);
            gst_object_unref(enc_src);
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
        gst_object_unref(audio_pad);
        gst_object_unref(enc_src);
    }
    
    /* 添加复用器和输出 */
    gst_bin_add_many(GST_BIN(pipeline), transcoder->muxer, transcoder->sink, NULL);
    
    /* 链接复用器到输出 */
    if (!gst_element_link(transcoder->muxer, transcoder->sink)) {
        LOG_ERROR("Failed to link muxer to sink");
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_LINK;
    }
    
    /* 连接 pad-added 信号 */
    g_signal_connect(transcoder->uridecodebin, "pad-added", 
                     G_CALLBACK(pad_added_handler), transcoder);
    
    transcoder->pipeline = pipeline;
    
    /* 设置消息总线 */
    transcoder->bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    transcoder->bus_watch_id = gst_bus_add_watch(transcoder->bus, bus_callback, transcoder);
    gst_object_unref(transcoder->bus);
    
    /* 设置进度更新定时器 */
    transcoder->progress_id = g_timeout_add(500, progress_callback, transcoder);
    
    LOG_INFO("Transcoding pipeline built successfully");
    return MEDIA_OK;
}

/**
 * @brief 开始转码
 */
MediaErrorCode transcoder_start(MediaTranscoder *transcoder) {
    if (transcoder == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&transcoder->mutex);
    
    /* 如果管道不存在，创建管道 */
    if (transcoder->pipeline == NULL) {
        MediaErrorCode ret = build_pipeline(transcoder);
        if (ret != MEDIA_OK) {
            pthread_mutex_unlock(&transcoder->mutex);
            return ret;
        }
    }
    
    LOG_INFO("Starting transcoding: %s -> %s", 
             transcoder->config.input_file, transcoder->config.output_file);
    
    /* 开始转码 */
    GstStateChangeReturn ret = gst_element_set_state(transcoder->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to start transcoding");
        pthread_mutex_unlock(&transcoder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    transcoder->state = MEDIA_STATE_PLAYING;
    transcoder->start_time = g_get_monotonic_time();
    
    /* 获取时长 */
    /* 等待管道进入播放状态 */
    GstState state;
    gst_element_get_state(transcoder->pipeline, &state, NULL, 5 * GST_SECOND);
    
    gint64 duration;
    if (gst_element_query_duration(transcoder->pipeline, GST_FORMAT_TIME, &duration)) {
        transcoder->progress.duration = duration;
        transcoder->input_info.duration = duration;
        LOG_INFO("Input duration: %.2f seconds", duration / 1000000000.0);
    }
    
    if (transcoder->callbacks.on_state_changed) {
        transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_PLAYING,
                                               transcoder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&transcoder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 暂停转码
 */
MediaErrorCode transcoder_pause(MediaTranscoder *transcoder) {
    if (transcoder == NULL || transcoder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&transcoder->mutex);
    
    LOG_INFO("Pausing transcoding");
    
    GstStateChangeReturn ret = gst_element_set_state(transcoder->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to pause transcoding");
        pthread_mutex_unlock(&transcoder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    transcoder->state = MEDIA_STATE_PAUSED;
    
    if (transcoder->callbacks.on_state_changed) {
        transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_PAUSED,
                                               transcoder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&transcoder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 恢复转码
 */
MediaErrorCode transcoder_resume(MediaTranscoder *transcoder) {
    if (transcoder == NULL || transcoder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&transcoder->mutex);
    
    LOG_INFO("Resuming transcoding");
    
    GstStateChangeReturn ret = gst_element_set_state(transcoder->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to resume transcoding");
        pthread_mutex_unlock(&transcoder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    transcoder->state = MEDIA_STATE_PLAYING;
    
    if (transcoder->callbacks.on_state_changed) {
        transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_PLAYING,
                                               transcoder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&transcoder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 停止转码
 */
MediaErrorCode transcoder_stop(MediaTranscoder *transcoder) {
    if (transcoder == NULL || transcoder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&transcoder->mutex);
    
    LOG_INFO("Stopping transcoding");
    
    /* 发送 EOS */
    gst_element_send_event(transcoder->pipeline, gst_event_new_eos());
    
    /* 等待 EOS 消息 */
    GstMessage *msg = gst_bus_timed_pop_filtered(transcoder->bus,
                                                  5 * GST_SECOND,
                                                  GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    
    if (msg != NULL) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *err = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(msg, &err, &debug);
            LOG_ERROR("Error during stop: %s", err->message);
            g_error_free(err);
            g_free(debug);
        }
        gst_message_unref(msg);
    }
    
    /* 停止管道 */
    gst_element_set_state(transcoder->pipeline, GST_STATE_NULL);
    
    /* 获取文件大小 */
    struct stat st;
    if (stat(transcoder->config.output_file, &st) == 0) {
        LOG_INFO("Transcoding completed: %s (%ld bytes)",
                transcoder->config.output_file, st.st_size);
    }
    
    /* 销毁管道 */
    if (transcoder->bus_watch_id > 0) {
        g_source_remove(transcoder->bus_watch_id);
        transcoder->bus_watch_id = 0;
    }
    if (transcoder->progress_id > 0) {
        g_source_remove(transcoder->progress_id);
        transcoder->progress_id = 0;
    }
    
    gst_object_unref(transcoder->pipeline);
    transcoder->pipeline = NULL;
    transcoder->uridecodebin = NULL;
    transcoder->video_queue = NULL;
    transcoder->video_enc = NULL;
    transcoder->audio_queue = NULL;
    transcoder->audio_enc = NULL;
    transcoder->muxer = NULL;
    transcoder->sink = NULL;
    
    transcoder->state = MEDIA_STATE_READY;
    
    if (transcoder->callbacks.on_state_changed) {
        transcoder->callbacks.on_state_changed(transcoder, MEDIA_STATE_READY,
                                               transcoder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&transcoder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 获取当前转码状态
 */
MediaState transcoder_get_state(MediaTranscoder *transcoder) {
    if (transcoder == NULL) {
        return MEDIA_STATE_NULL;
    }
    return transcoder->state;
}

/**
 * @brief 获取转码进度
 */
MediaErrorCode transcoder_get_progress(MediaTranscoder *transcoder, TranscodeProgress *progress) {
    if (transcoder == NULL || progress == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    memcpy(progress, &transcoder->progress, sizeof(TranscodeProgress));
    return MEDIA_OK;
}

/**
 * @brief 获取输入文件信息
 */
MediaErrorCode transcoder_get_input_info(MediaTranscoder *transcoder, MediaInfo *info) {
    if (transcoder == NULL || info == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    memcpy(info, &transcoder->input_info, sizeof(MediaInfo));
    return MEDIA_OK;
}

/**
 * @brief 设置视频转码参数
 */
MediaErrorCode transcoder_set_video_params(MediaTranscoder *transcoder, const VideoParams *params) {
    if (transcoder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (transcoder->state != MEDIA_STATE_NULL && transcoder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    memcpy(&transcoder->config.video_params, params, sizeof(VideoParams));
    return MEDIA_OK;
}

/**
 * @brief 设置音频转码参数
 */
MediaErrorCode transcoder_set_audio_params(MediaTranscoder *transcoder, const AudioParams *params) {
    if (transcoder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (transcoder->state != MEDIA_STATE_NULL && transcoder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    memcpy(&transcoder->config.audio_params, params, sizeof(AudioParams));
    return MEDIA_OK;
}

/**
 * @brief 设置转码范围
 */
MediaErrorCode transcoder_set_range(MediaTranscoder *transcoder, int64_t start_time, int64_t end_time) {
    if (transcoder == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (transcoder->state != MEDIA_STATE_NULL && transcoder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    transcoder->config.start_time = start_time;
    transcoder->config.end_time = end_time;
    return MEDIA_OK;
}
