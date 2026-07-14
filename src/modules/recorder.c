/**
 * @file recorder.c
 * @brief 音视频录制模块实现
 * @details 基于 GStreamer 实现完整的音视频录制功能
 */

#include "recorder.h"
#include "logger.h"
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/**
 * @brief 录制器结构体定义
 */
struct MediaRecorder {
    RecorderConfig config;          /**< 配置 */
    RecorderCallbacks callbacks;    /**< 回调 */
    
    GstElement *pipeline;           /**< GStreamer 管道 */
    GstElement *video_src;          /**< 视频源 */
    GstElement *video_enc;          /**< 视频编码器 */
    GstElement *audio_src;          /**< 音频源 */
    GstElement *audio_enc;          /**< 音频编码器 */
    GstElement *muxer;              /**< 复用器 */
    GstElement *sink;               /**< 输出 */
    
    GstBus *bus;                    /**< 消息总线 */
    guint bus_watch_id;             /**< 总线监听ID */
    
    MediaState state;               /**< 当前状态 */
    
    pthread_mutex_t mutex;          /**< 互斥锁 */
    
    int64_t start_time;             /**< 开始时间 */
    int64_t duration;               /**< 录制时长 */
    int64_t file_size;              /**< 文件大小 */
    
    GstClockTime base_time;         /**< 基准时间 */
};

/* 前向声明 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data);
static GstElement *create_video_encoder(VideoCodec codec, const VideoParams *params);
static GstElement *create_audio_encoder(AudioCodec codec, const AudioParams *params);
static GstElement *create_muxer(ContainerFormat format);

static GstPad *request_muxer_pad(GstElement *muxer, const char *name) {
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_element_request_pad_simple(muxer, name);
#else
    return gst_element_get_request_pad(muxer, name);
#endif
}

/**
 * @brief 初始化录制器配置为默认值
 */
void recorder_config_init(RecorderConfig *config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(RecorderConfig));
    
    /* 默认视频源 */
    config->video_source = VIDEO_SOURCE_V4L2;
    strcpy(config->video_device, "/dev/video0");
    video_params_init(&config->video_params);
    config->video_params.width = 1920;
    config->video_params.height = 1080;
    config->video_params.framerate_num = 30;
    config->video_params.framerate_den = 1;
    config->video_params.codec = VIDEO_CODEC_H264;
    config->video_params.bitrate = 4000000;
    
    /* 默认音频源 */
    config->audio_source = AUDIO_SOURCE_ALSA;
    strcpy(config->audio_device, "default");
    audio_params_init(&config->audio_params);
    config->audio_params.codec = AUDIO_CODEC_AAC;
    config->audio_params.bitrate = 128000;
    
    /* 默认输出 */
    strcpy(config->output_file, "output.mp4");
    config->container = CONTAINER_MP4;
    
    /* 性能配置 */
    config->low_latency = 1;
    config->buffer_size = 1024 * 1024;
    config->enable_hardware_encode = 1;
    config->realtime = 1;
    
    /* 录制限制 */
    config->max_duration = 0;  /* 无限制 */
    config->max_size = 0;      /* 无限制 */
}

/**
 * @brief 初始化录制器回调为默认值
 */
void recorder_callbacks_init(RecorderCallbacks *callbacks) {
    if (callbacks == NULL) {
        return;
    }
    
    memset(callbacks, 0, sizeof(RecorderCallbacks));
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
                /* 设置编码参数 */
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "bitrate")) {
                    g_object_set(encoder, "bitrate", params->bitrate / 1000, NULL);
                }
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "key-int-max")) {
                    g_object_set(encoder, "key-int-max", params->gop_size, NULL);
                }
                /* 低延迟设置 */
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "tune")) {
                    g_object_set(encoder, "tune", 4, NULL);  /* zerolatency */
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
            if (encoder == NULL) {
                encoder = gst_element_factory_make("lame", encoder_name);
            }
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
            
        case AUDIO_CODEC_FLAC:
            encoder = gst_element_factory_make("flacenc", encoder_name);
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
            
        case CONTAINER_FLV:
            muxer = gst_element_factory_make("flvmux", "muxer");
            break;
            
        default:
            LOG_WARN("Unsupported container format: %s", container_format_to_string(format));
            muxer = gst_element_factory_make("mp4mux", "muxer");
            break;
    }
    
    return muxer;
}

/**
 * @brief GStreamer 消息总线回调
 */
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    (void)bus;
    MediaRecorder *recorder = (MediaRecorder *)data;
    
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &err, &debug);
            
            LOG_ERROR("Recorder error: %s (%s)", err->message, debug);
            
            if (recorder->callbacks.on_error) {
                recorder->callbacks.on_error(recorder, MEDIA_ERROR_GST_MESSAGE_ERROR,
                                            err->message, recorder->callbacks.user_data);
            }
            
            g_error_free(err);
            g_free(debug);
            
            recorder->state = MEDIA_STATE_ERROR;
            if (recorder->callbacks.on_state_changed) {
                recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_ERROR,
                                                     recorder->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_EOS: {
            LOG_INFO("Recording finished (EOS)");
            recorder->state = MEDIA_STATE_READY;
            
            if (recorder->callbacks.on_state_changed) {
                recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_READY,
                                                     recorder->callbacks.user_data);
            }
            break;
        }
        
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(recorder->pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
                (void)pending_state;
                
                LOG_DEBUG("Recorder state changed: %s -> %s",
                         gst_element_state_get_name(old_state),
                         gst_element_state_get_name(new_state));
            }
            break;
        }
        
        case GST_MESSAGE_ELEMENT: {
            const GstStructure *s = gst_message_get_structure(message);
            if (gst_structure_has_name(s, "GstBinForwarded")) {
                /* 处理转发的消息 */
            }
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}

/**
 * @brief 创建录制器
 */
MediaRecorder *recorder_create(const RecorderConfig *config) {
    gst_init(NULL, NULL);

    MediaRecorder *recorder = (MediaRecorder *)malloc(sizeof(MediaRecorder));
    if (recorder == NULL) {
        LOG_ERROR("Failed to allocate memory for recorder");
        return NULL;
    }
    
    memset(recorder, 0, sizeof(MediaRecorder));
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&recorder->config, config, sizeof(RecorderConfig));
    } else {
        recorder_config_init(&recorder->config);
    }
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&recorder->mutex, NULL);
    
    /* 初始化状态 */
    recorder->state = MEDIA_STATE_NULL;
    
    LOG_INFO("Recorder created successfully");
    return recorder;
}

/**
 * @brief 销毁录制器
 */
void recorder_destroy(MediaRecorder *recorder) {
    if (recorder == NULL) {
        return;
    }
    
    LOG_INFO("Destroying recorder");
    
    /* 停止录制 */
    if (recorder->state == MEDIA_STATE_PLAYING) {
        recorder_stop(recorder);
    }
    
    /* 销毁管道 */
    if (recorder->pipeline != NULL) {
        gst_element_set_state(recorder->pipeline, GST_STATE_NULL);
        
        if (recorder->bus_watch_id > 0) {
            g_source_remove(recorder->bus_watch_id);
        }
        if (recorder->bus != NULL) {
            gst_object_unref(recorder->bus);
            recorder->bus = NULL;
        }
        
        gst_object_unref(recorder->pipeline);
    }
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&recorder->mutex);
    
    free(recorder);
    LOG_INFO("Recorder destroyed");
}

/**
 * @brief 设置录制器回调
 */
MediaErrorCode recorder_set_callbacks(MediaRecorder *recorder, const RecorderCallbacks *callbacks) {
    if (recorder == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (callbacks != NULL) {
        memcpy(&recorder->callbacks, callbacks, sizeof(RecorderCallbacks));
    } else {
        recorder_callbacks_init(&recorder->callbacks);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 构建录制管道
 */
static MediaErrorCode build_pipeline(MediaRecorder *recorder) {
    GstElement *pipeline, *video_queue, *audio_queue;
    GstElement *video_convert = NULL, *audio_convert = NULL;
    GstCaps *video_caps = NULL, *audio_caps = NULL;
    
    /* 创建管道 */
    pipeline = gst_pipeline_new("recorder_pipeline");
    if (pipeline == NULL) {
        LOG_ERROR("Failed to create pipeline");
        return MEDIA_ERROR_RECORDER_CREATE_PIPELINE;
    }
    
    /* 创建视频源 */
    switch (recorder->config.video_source) {
        case VIDEO_SOURCE_V4L2:
            recorder->video_src = gst_element_factory_make("v4l2src", "video_src");
            if (recorder->video_src == NULL) {
                LOG_ERROR("Failed to create v4l2src");
                gst_object_unref(pipeline);
                return MEDIA_ERROR_GST_ELEMENT_CREATE;
            }
            g_object_set(recorder->video_src, "device", recorder->config.video_device, NULL);
            
            /* 设置视频格式 */
            video_caps = gst_caps_new_simple("video/x-raw",
                "width", G_TYPE_INT, recorder->config.video_params.width,
                "height", G_TYPE_INT, recorder->config.video_params.height,
                "framerate", GST_TYPE_FRACTION, 
                    recorder->config.video_params.framerate_num,
                    recorder->config.video_params.framerate_den,
                "format", G_TYPE_STRING, "I420",
                NULL);
            
            video_convert = gst_element_factory_make("videoconvert", "video_convert");
            break;
            
        case VIDEO_SOURCE_TEST:
            recorder->video_src = gst_element_factory_make("videotestsrc", "video_src");
            video_caps = gst_caps_new_simple("video/x-raw",
                "width", G_TYPE_INT, recorder->config.video_params.width,
                "height", G_TYPE_INT, recorder->config.video_params.height,
                "framerate", GST_TYPE_FRACTION, 
                    recorder->config.video_params.framerate_num,
                    recorder->config.video_params.framerate_den,
                NULL);
            video_convert = gst_element_factory_make("videoconvert", "video_convert");
            break;
            
        default:
            break;
    }
    
    /* 创建音频源 */
    switch (recorder->config.audio_source) {
        case AUDIO_SOURCE_ALSA:
            recorder->audio_src = gst_element_factory_make("alsasrc", "audio_src");
            if (recorder->audio_src == NULL) {
                LOG_WARN("Failed to create alsasrc, trying pulsesrc");
                recorder->audio_src = gst_element_factory_make("pulsesrc", "audio_src");
            }
            if (recorder->audio_src != NULL) {
                g_object_set(recorder->audio_src, "device", recorder->config.audio_device, NULL);
            }
            
            audio_caps = gst_caps_new_simple("audio/x-raw",
                "rate", G_TYPE_INT, recorder->config.audio_params.sample_rate,
                "channels", G_TYPE_INT, recorder->config.audio_params.channels,
                "format", G_TYPE_STRING, "S16LE",
                NULL);
            audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
            break;
            
        case AUDIO_SOURCE_PULSE:
            recorder->audio_src = gst_element_factory_make("pulsesrc", "audio_src");
            audio_caps = gst_caps_new_simple("audio/x-raw",
                "rate", G_TYPE_INT, recorder->config.audio_params.sample_rate,
                "channels", G_TYPE_INT, recorder->config.audio_params.channels,
                NULL);
            audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
            break;
            
        case AUDIO_SOURCE_TEST:
            recorder->audio_src = gst_element_factory_make("audiotestsrc", "audio_src");
            audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
            break;
            
        default:
            break;
    }
    
    /* 创建编码器 */
    if (recorder->video_src != NULL) {
        recorder->video_enc = create_video_encoder(recorder->config.video_params.codec,
                                                   &recorder->config.video_params);
        if (recorder->video_enc == NULL) {
            LOG_ERROR("Failed to create video encoder");
            if (video_caps) gst_caps_unref(video_caps);
            if (audio_caps) gst_caps_unref(audio_caps);
            gst_object_unref(pipeline);
            return MEDIA_ERROR_RECORDER_NO_ENCODER;
        }
    }
    
    if (recorder->audio_src != NULL) {
        recorder->audio_enc = create_audio_encoder(recorder->config.audio_params.codec,
                                                   &recorder->config.audio_params);
        if (recorder->audio_enc == NULL) {
            LOG_ERROR("Failed to create audio encoder");
            if (video_caps) gst_caps_unref(video_caps);
            if (audio_caps) gst_caps_unref(audio_caps);
            gst_object_unref(pipeline);
            return MEDIA_ERROR_RECORDER_NO_ENCODER;
        }
    }
    
    /* 创建复用器和输出 */
    recorder->muxer = create_muxer(recorder->config.container);
    recorder->sink = gst_element_factory_make("filesink", "sink");
    if (recorder->sink == NULL) {
        LOG_ERROR("Failed to create filesink");
        if (video_caps) gst_caps_unref(video_caps);
        if (audio_caps) gst_caps_unref(audio_caps);
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_CREATE;
    }
    g_object_set(recorder->sink, "location", recorder->config.output_file, NULL);
    
    /* 创建队列 */
    video_queue = gst_element_factory_make("queue", "video_queue");
    audio_queue = gst_element_factory_make("queue", "audio_queue");
    
    /* 添加元素到管道 */
    gst_bin_add_many(GST_BIN(pipeline),
                    recorder->video_src, video_convert, video_queue, 
                    recorder->video_enc,
                    recorder->audio_src, audio_convert, audio_queue,
                    recorder->audio_enc,
                    recorder->muxer, recorder->sink,
                    NULL);
    
    /* 链接视频分支 */
    if (recorder->video_src != NULL && recorder->video_enc != NULL) {
        if (video_caps != NULL) {
            GstElement *capsfilter = gst_element_factory_make("capsfilter", "video_caps");
            g_object_set(capsfilter, "caps", video_caps, NULL);
            gst_bin_add(GST_BIN(pipeline), capsfilter);
            
            if (!gst_element_link_many(recorder->video_src, capsfilter, video_convert,
                                       video_queue, recorder->video_enc, NULL)) {
                LOG_ERROR("Failed to link video elements");
                gst_caps_unref(video_caps);
                gst_object_unref(pipeline);
                return MEDIA_ERROR_GST_ELEMENT_LINK;
            }
        } else {
            if (!gst_element_link_many(recorder->video_src, video_convert,
                                       video_queue, recorder->video_enc, NULL)) {
                LOG_ERROR("Failed to link video elements");
                gst_object_unref(pipeline);
                return MEDIA_ERROR_GST_ELEMENT_LINK;
            }
        }
        
        /* 链接视频编码器到复用器 */
        GstPad *video_pad = request_muxer_pad(recorder->muxer, "video_%u");
        GstPad *enc_src = gst_element_get_static_pad(recorder->video_enc, "src");
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
    
    /* 链接音频分支 */
    if (recorder->audio_src != NULL && recorder->audio_enc != NULL) {
        if (audio_caps != NULL) {
            GstElement *capsfilter = gst_element_factory_make("capsfilter", "audio_caps");
            g_object_set(capsfilter, "caps", audio_caps, NULL);
            gst_bin_add(GST_BIN(pipeline), capsfilter);
            
            if (!gst_element_link_many(recorder->audio_src, capsfilter, audio_convert,
                                       audio_queue, recorder->audio_enc, NULL)) {
                LOG_ERROR("Failed to link audio elements");
                gst_caps_unref(audio_caps);
                gst_object_unref(pipeline);
                return MEDIA_ERROR_GST_ELEMENT_LINK;
            }
        } else {
            if (!gst_element_link_many(recorder->audio_src, audio_convert,
                                       audio_queue, recorder->audio_enc, NULL)) {
                LOG_ERROR("Failed to link audio elements");
                gst_object_unref(pipeline);
                return MEDIA_ERROR_GST_ELEMENT_LINK;
            }
        }
        
        /* 链接音频编码器到复用器 */
        GstPad *audio_pad = request_muxer_pad(recorder->muxer, "audio_%u");
        GstPad *enc_src = gst_element_get_static_pad(recorder->audio_enc, "src");
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
    
    /* 链接复用器到输出 */
    if (!gst_element_link(recorder->muxer, recorder->sink)) {
        LOG_ERROR("Failed to link muxer to sink");
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_LINK;
    }
    
    /* 清理 */
    if (video_caps) gst_caps_unref(video_caps);
    if (audio_caps) gst_caps_unref(audio_caps);
    
    recorder->pipeline = pipeline;
    
    /* 设置消息总线 */
    recorder->bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    recorder->bus_watch_id = gst_bus_add_watch(recorder->bus, bus_callback, recorder);
    
    LOG_INFO("Recording pipeline built successfully");
    return MEDIA_OK;
}

/**
 * @brief 开始录制
 */
MediaErrorCode recorder_start(MediaRecorder *recorder) {
    if (recorder == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&recorder->mutex);
    
    /* 如果管道不存在，创建管道 */
    if (recorder->pipeline == NULL) {
        MediaErrorCode ret = build_pipeline(recorder);
        if (ret != MEDIA_OK) {
            pthread_mutex_unlock(&recorder->mutex);
            return ret;
        }
    }
    
    LOG_INFO("Starting recording to %s", recorder->config.output_file);
    
    /* 开始录制 */
    GstStateChangeReturn ret = gst_element_set_state(recorder->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to start recording");
        pthread_mutex_unlock(&recorder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    recorder->state = MEDIA_STATE_PLAYING;
    recorder->start_time = g_get_monotonic_time();
    
    if (recorder->callbacks.on_state_changed) {
        recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_PLAYING,
                                             recorder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&recorder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 暂停录制
 */
MediaErrorCode recorder_pause(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&recorder->mutex);
    
    LOG_INFO("Pausing recording");
    
    GstStateChangeReturn ret = gst_element_set_state(recorder->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to pause recording");
        pthread_mutex_unlock(&recorder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    recorder->state = MEDIA_STATE_PAUSED;
    
    if (recorder->callbacks.on_state_changed) {
        recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_PAUSED,
                                             recorder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&recorder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 恢复录制
 */
MediaErrorCode recorder_resume(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&recorder->mutex);
    
    LOG_INFO("Resuming recording");
    
    GstStateChangeReturn ret = gst_element_set_state(recorder->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("Failed to resume recording");
        pthread_mutex_unlock(&recorder->mutex);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    
    recorder->state = MEDIA_STATE_PLAYING;
    
    if (recorder->callbacks.on_state_changed) {
        recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_PLAYING,
                                             recorder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&recorder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 停止录制
 */
MediaErrorCode recorder_stop(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->pipeline == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&recorder->mutex);
    
    LOG_INFO("Stopping recording");
    
    /* 发送 EOS 以确保文件正确结束 */
    gst_element_send_event(recorder->pipeline, gst_event_new_eos());
    
    /* 等待 EOS 消息 */
    GstMessage *msg = gst_bus_timed_pop_filtered(recorder->bus,
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
    gst_element_set_state(recorder->pipeline, GST_STATE_NULL);
    
    /* 计算录制时长 */
    recorder->duration = (g_get_monotonic_time() - recorder->start_time) * 1000;
    
    /* 获取文件大小 */
    struct stat st;
    if (stat(recorder->config.output_file, &st) == 0) {
        recorder->file_size = st.st_size;
        LOG_INFO("Recording saved: %s (%ld bytes, %.2f seconds)",
                recorder->config.output_file, recorder->file_size,
                recorder->duration / 1000000000.0);
    }
    
    /* 销毁管道 */
    if (recorder->bus_watch_id > 0) {
        g_source_remove(recorder->bus_watch_id);
        recorder->bus_watch_id = 0;
    }
    gst_object_unref(recorder->bus);
    recorder->bus = NULL;
    
    gst_object_unref(recorder->pipeline);
    recorder->pipeline = NULL;
    recorder->video_src = NULL;
    recorder->video_enc = NULL;
    recorder->audio_src = NULL;
    recorder->audio_enc = NULL;
    recorder->muxer = NULL;
    recorder->sink = NULL;
    
    recorder->state = MEDIA_STATE_READY;
    
    if (recorder->callbacks.on_state_changed) {
        recorder->callbacks.on_state_changed(recorder, MEDIA_STATE_READY,
                                             recorder->callbacks.user_data);
    }
    
    pthread_mutex_unlock(&recorder->mutex);
    return MEDIA_OK;
}

/**
 * @brief 获取当前录制状态
 */
MediaState recorder_get_state(MediaRecorder *recorder) {
    if (recorder == NULL) {
        return MEDIA_STATE_NULL;
    }
    return recorder->state;
}

/**
 * @brief 获取录制时长
 */
int64_t recorder_get_duration(MediaRecorder *recorder) {
    if (recorder == NULL) {
        return 0;
    }
    
    if (recorder->state == MEDIA_STATE_PLAYING) {
        return (g_get_monotonic_time() - recorder->start_time) * 1000;
    }
    
    return recorder->duration;
}

/**
 * @brief 获取录制文件大小
 */
int64_t recorder_get_file_size(MediaRecorder *recorder) {
    if (recorder == NULL) {
        return 0;
    }
    
    struct stat st;
    if (stat(recorder->config.output_file, &st) == 0) {
        recorder->file_size = st.st_size;
    }
    
    return recorder->file_size;
}

/**
 * @brief 设置视频参数
 */
MediaErrorCode recorder_set_video_params(MediaRecorder *recorder, const VideoParams *params) {
    if (recorder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (recorder->state != MEDIA_STATE_NULL && recorder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    memcpy(&recorder->config.video_params, params, sizeof(VideoParams));
    return MEDIA_OK;
}

/**
 * @brief 设置音频参数
 */
MediaErrorCode recorder_set_audio_params(MediaRecorder *recorder, const AudioParams *params) {
    if (recorder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (recorder->state != MEDIA_STATE_NULL && recorder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    memcpy(&recorder->config.audio_params, params, sizeof(AudioParams));
    return MEDIA_OK;
}

/**
 * @brief 设置输出文件
 */
MediaErrorCode recorder_set_output_file(MediaRecorder *recorder, const char *file_path) {
    if (recorder == NULL || file_path == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (recorder->state != MEDIA_STATE_NULL && recorder->state != MEDIA_STATE_READY) {
        return MEDIA_ERROR_BUSY;
    }
    
    strncpy(recorder->config.output_file, file_path, 
            sizeof(recorder->config.output_file) - 1);
    return MEDIA_OK;
}

/**
 * @brief 枚举可用视频设备
 */
int recorder_enum_video_devices(char devices[][256], int max_count) {
    int count = 0;
    DIR *dir;
    struct dirent *entry;
    
    dir = opendir("/dev");
    if (dir == NULL) {
        LOG_ERROR("Failed to open /dev directory");
        return 0;
    }
    
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (strncmp(entry->d_name, "video", 5) == 0) {
            snprintf(devices[count], 256, "/dev/%.250s", entry->d_name);
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

/**
 * @brief 枚举可用音频设备
 */
int recorder_enum_audio_devices(char devices[][256], int max_count) {
    int count = 0;
    
    /* 添加默认设备 */
    if (count < max_count) {
        strcpy(devices[count++], "default");
    }
    
    /* 可以通过 ALSA API 获取更多设备 */
    /* 这里简化处理，只返回默认设备 */
    
    return count;
}
