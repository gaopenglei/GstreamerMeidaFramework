/**
 * @file media_controller.c
 * @brief 媒体控制器实现
 * @details 统一管理播放、录制、转码功能
 */

#include "media_controller.h"
#include "logger.h"
#include <gst/gst.h>
#include <pthread.h>

/**
 * @brief 媒体控制器结构体定义
 */
struct MediaController {
    ControllerConfig config;            /**< 配置 */
    ControllerCallbacks callbacks;      /**< 回调 */
    
    MediaPlayer *player;                /**< 播放器 */
    MediaRecorder *recorder;            /**< 录制器 */
    MediaTranscoder *transcoder;        /**< 转码器 */
    
    OperationType current_operation;    /**< 当前操作 */
    MediaState state;                   /**< 当前状态 */
    
    pthread_mutex_t mutex;              /**< 互斥锁 */
    int initialized;                    /**< 初始化标志 */
};

/* 前向声明 */
static void player_state_callback(MediaPlayer *player, MediaState state, void *user_data);
static void recorder_state_callback(MediaRecorder *recorder, MediaState state, void *user_data);
static void transcoder_state_callback(MediaTranscoder *transcoder, MediaState state, void *user_data);

/**
 * @brief 初始化控制器配置为默认值
 */
void controller_config_init(ControllerConfig *config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(ControllerConfig));
    strcpy(config->log_file, "/var/log/media_framework.log");
    config->log_level = LOG_LEVEL_INFO;
    config->log_to_console = 1;
    config->log_to_file = 0;
    config->enable_hardware = 1;
    config->low_latency = 1;
}

/**
 * @brief 初始化控制器回调为默认值
 */
void controller_callbacks_init(ControllerCallbacks *callbacks) {
    if (callbacks == NULL) {
        return;
    }
    
    memset(callbacks, 0, sizeof(ControllerCallbacks));
}

/**
 * @brief 播放器状态回调
 */
static void player_state_callback(MediaPlayer *player, MediaState state, void *user_data) {
    MediaController *controller = (MediaController *)user_data;
    
    controller->state = state;
    
    if (controller->callbacks.on_state_changed) {
        controller->callbacks.on_state_changed(controller, OPERATION_PLAY, state,
                                               controller->callbacks.user_data);
    }
}

/**
 * @brief 录制器状态回调
 */
static void recorder_state_callback(MediaRecorder *recorder, MediaState state, void *user_data) {
    MediaController *controller = (MediaController *)user_data;
    
    controller->state = state;
    
    if (controller->callbacks.on_state_changed) {
        controller->callbacks.on_state_changed(controller, OPERATION_RECORD, state,
                                               controller->callbacks.user_data);
    }
}

/**
 * @brief 转码器状态回调
 */
static void transcoder_state_callback(MediaTranscoder *transcoder, MediaState state, void *user_data) {
    MediaController *controller = (MediaController *)user_data;
    
    controller->state = state;
    
    if (controller->callbacks.on_state_changed) {
        controller->callbacks.on_state_changed(controller, OPERATION_TRANSCODE, state,
                                               controller->callbacks.user_data);
    }
}

/**
 * @brief 创建媒体控制器
 */
MediaController *controller_create(const ControllerConfig *config) {
    MediaController *controller = (MediaController *)malloc(sizeof(MediaController));
    if (controller == NULL) {
        fprintf(stderr, "Failed to allocate memory for controller\n");
        return NULL;
    }
    
    memset(controller, 0, sizeof(MediaController));
    
    /* 设置配置 */
    if (config != NULL) {
        memcpy(&controller->config, config, sizeof(ControllerConfig));
    } else {
        controller_config_init(&controller->config);
    }
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&controller->mutex, NULL);
    
    /* 初始化 GStreamer */
    gst_init(NULL, NULL);
    
    /* 初始化日志模块 */
    LoggerConfig log_config;
    logger_config_init(&log_config);
    log_config.min_level = controller->config.log_level;
    log_config.target = controller->config.log_to_console ? 
                        (controller->config.log_to_file ? LOG_TARGET_BOTH : LOG_TARGET_CONSOLE) :
                        LOG_TARGET_FILE;
    if (controller->config.log_to_file) {
        strncpy(log_config.log_file, controller->config.log_file, sizeof(log_config.log_file) - 1);
    }
    logger_init(&log_config);
    
    /* 创建播放器 */
    PlayerConfig player_config;
    player_config_init(&player_config);
    player_config.low_latency = controller->config.low_latency;
    player_config.enable_hardware_decode = controller->config.enable_hardware;
    controller->player = player_create(&player_config);
    
    if (controller->player != NULL) {
        PlayerCallbacks player_callbacks;
        player_callbacks_init(&player_callbacks);
        player_callbacks.on_state_changed = player_state_callback;
        player_callbacks.user_data = controller;
        player_set_callbacks(controller->player, &player_callbacks);
    }
    
    /* 创建录制器 */
    controller->recorder = recorder_create(NULL);
    
    if (controller->recorder != NULL) {
        RecorderCallbacks recorder_callbacks;
        recorder_callbacks_init(&recorder_callbacks);
        recorder_callbacks.on_state_changed = recorder_state_callback;
        recorder_callbacks.user_data = controller;
        recorder_set_callbacks(controller->recorder, &recorder_callbacks);
    }
    
    /* 创建转码器 */
    controller->transcoder = transcoder_create(NULL);
    
    if (controller->transcoder != NULL) {
        TranscoderCallbacks transcoder_callbacks;
        transcoder_callbacks_init(&transcoder_callbacks);
        transcoder_callbacks.on_state_changed = transcoder_state_callback;
        transcoder_callbacks.user_data = controller;
        transcoder_set_callbacks(controller->transcoder, &transcoder_callbacks);
    }
    
    controller->current_operation = OPERATION_NONE;
    controller->state = MEDIA_STATE_NULL;
    controller->initialized = 1;
    
    LOG_INFO("Media controller created successfully");
    return controller;
}

/**
 * @brief 销毁媒体控制器
 */
void controller_destroy(MediaController *controller) {
    if (controller == NULL) {
        return;
    }
    
    LOG_INFO("Destroying media controller");
    
    /* 停止所有操作 */
    controller_stop(controller);
    
    /* 销毁播放器 */
    if (controller->player != NULL) {
        player_destroy(controller->player);
        controller->player = NULL;
    }
    
    /* 销毁录制器 */
    if (controller->recorder != NULL) {
        recorder_destroy(controller->recorder);
        controller->recorder = NULL;
    }
    
    /* 销毁转码器 */
    if (controller->transcoder != NULL) {
        transcoder_destroy(controller->transcoder);
        controller->transcoder = NULL;
    }
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&controller->mutex);
    
    /* 关闭日志 */
    logger_shutdown();
    
    free(controller);
    LOG_INFO("Media controller destroyed");
}

/**
 * @brief 设置控制器回调
 */
MediaErrorCode controller_set_callbacks(MediaController *controller, 
                                        const ControllerCallbacks *callbacks) {
    if (controller == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (callbacks != NULL) {
        memcpy(&controller->callbacks, callbacks, sizeof(ControllerCallbacks));
    } else {
        controller_callbacks_init(&controller->callbacks);
    }
    
    return MEDIA_OK;
}

/**
 * @brief 获取播放器实例
 */
MediaPlayer *controller_get_player(MediaController *controller) {
    if (controller == NULL) {
        return NULL;
    }
    return controller->player;
}

/**
 * @brief 获取录制器实例
 */
MediaRecorder *controller_get_recorder(MediaController *controller) {
    if (controller == NULL) {
        return NULL;
    }
    return controller->recorder;
}

/**
 * @brief 获取转码器实例
 */
MediaTranscoder *controller_get_transcoder(MediaController *controller) {
    if (controller == NULL) {
        return NULL;
    }
    return controller->transcoder;
}

/**
 * @brief 播放文件
 */
MediaErrorCode controller_play(MediaController *controller, const char *uri) {
    if (controller == NULL || uri == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (controller->player == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    /* 停止当前操作 */
    if (controller->current_operation != OPERATION_NONE) {
        controller_stop(controller);
    }
    
    LOG_INFO("Playing: %s", uri);
    
    MediaErrorCode ret = player_set_uri(controller->player, uri);
    if (ret != MEDIA_OK) {
        pthread_mutex_unlock(&controller->mutex);
        return ret;
    }
    
    ret = player_play(controller->player);
    if (ret == MEDIA_OK) {
        controller->current_operation = OPERATION_PLAY;
        controller->state = MEDIA_STATE_PLAYING;
    }
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 暂停当前操作
 */
MediaErrorCode controller_pause(MediaController *controller) {
    if (controller == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    MediaErrorCode ret = MEDIA_OK;
    
    switch (controller->current_operation) {
        case OPERATION_PLAY:
            if (controller->player != NULL) {
                ret = player_pause(controller->player);
            }
            break;
            
        case OPERATION_RECORD:
            if (controller->recorder != NULL) {
                ret = recorder_pause(controller->recorder);
            }
            break;
            
        case OPERATION_TRANSCODE:
            if (controller->transcoder != NULL) {
                ret = transcoder_pause(controller->transcoder);
            }
            break;
            
        default:
            ret = MEDIA_ERROR_NOT_SUPPORTED;
            break;
    }
    
    if (ret == MEDIA_OK) {
        controller->state = MEDIA_STATE_PAUSED;
    }
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 恢复当前操作
 */
MediaErrorCode controller_resume(MediaController *controller) {
    if (controller == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    MediaErrorCode ret = MEDIA_OK;
    
    switch (controller->current_operation) {
        case OPERATION_PLAY:
            if (controller->player != NULL) {
                ret = player_play(controller->player);
            }
            break;
            
        case OPERATION_RECORD:
            if (controller->recorder != NULL) {
                ret = recorder_resume(controller->recorder);
            }
            break;
            
        case OPERATION_TRANSCODE:
            if (controller->transcoder != NULL) {
                ret = transcoder_resume(controller->transcoder);
            }
            break;
            
        default:
            ret = MEDIA_ERROR_NOT_SUPPORTED;
            break;
    }
    
    if (ret == MEDIA_OK) {
        controller->state = MEDIA_STATE_PLAYING;
    }
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 停止当前操作
 */
MediaErrorCode controller_stop(MediaController *controller) {
    if (controller == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    MediaErrorCode ret = MEDIA_OK;
    
    switch (controller->current_operation) {
        case OPERATION_PLAY:
            if (controller->player != NULL) {
                ret = player_stop(controller->player);
            }
            break;
            
        case OPERATION_RECORD:
            if (controller->recorder != NULL) {
                ret = recorder_stop(controller->recorder);
            }
            break;
            
        case OPERATION_TRANSCODE:
            if (controller->transcoder != NULL) {
                ret = transcoder_stop(controller->transcoder);
            }
            break;
            
        default:
            break;
    }
    
    controller->current_operation = OPERATION_NONE;
    controller->state = MEDIA_STATE_READY;
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 开始录制
 */
MediaErrorCode controller_start_recording(MediaController *controller, 
                                          const RecorderConfig *config) {
    if (controller == NULL || config == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (controller->recorder == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    /* 停止当前操作 */
    if (controller->current_operation != OPERATION_NONE) {
        controller_stop(controller);
    }
    
    LOG_INFO("Starting recording to: %s", config->output_file);
    
    /* 更新录制器配置 */
    MediaErrorCode ret = MEDIA_OK;
    
    /* 销毁旧录制器并创建新的 */
    if (controller->recorder != NULL) {
        recorder_destroy(controller->recorder);
    }
    
    controller->recorder = recorder_create(config);
    if (controller->recorder == NULL) {
        pthread_mutex_unlock(&controller->mutex);
        return MEDIA_ERROR_RECORDER_INIT_FAILED;
    }
    
    RecorderCallbacks recorder_callbacks;
    recorder_callbacks_init(&recorder_callbacks);
    recorder_callbacks.on_state_changed = recorder_state_callback;
    recorder_callbacks.user_data = controller;
    recorder_set_callbacks(controller->recorder, &recorder_callbacks);
    
    ret = recorder_start(controller->recorder);
    if (ret == MEDIA_OK) {
        controller->current_operation = OPERATION_RECORD;
        controller->state = MEDIA_STATE_PLAYING;
    }
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 开始转码
 */
MediaErrorCode controller_start_transcoding(MediaController *controller,
                                            const TranscoderConfig *config) {
    if (controller == NULL || config == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    
    if (controller->transcoder == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&controller->mutex);
    
    /* 停止当前操作 */
    if (controller->current_operation != OPERATION_NONE) {
        controller_stop(controller);
    }
    
    LOG_INFO("Starting transcoding: %s -> %s", config->input_file, config->output_file);
    
    /* 销毁旧转码器并创建新的 */
    if (controller->transcoder != NULL) {
        transcoder_destroy(controller->transcoder);
    }
    
    controller->transcoder = transcoder_create(config);
    if (controller->transcoder == NULL) {
        pthread_mutex_unlock(&controller->mutex);
        return MEDIA_ERROR_TRANSCODER_INIT_FAILED;
    }
    
    TranscoderCallbacks transcoder_callbacks;
    transcoder_callbacks_init(&transcoder_callbacks);
    transcoder_callbacks.on_state_changed = transcoder_state_callback;
    transcoder_callbacks.user_data = controller;
    transcoder_set_callbacks(controller->transcoder, &transcoder_callbacks);
    
    MediaErrorCode ret = transcoder_start(controller->transcoder);
    if (ret == MEDIA_OK) {
        controller->current_operation = OPERATION_TRANSCODE;
        controller->state = MEDIA_STATE_PLAYING;
    }
    
    pthread_mutex_unlock(&controller->mutex);
    return ret;
}

/**
 * @brief 获取当前操作类型
 */
OperationType controller_get_operation(MediaController *controller) {
    if (controller == NULL) {
        return OPERATION_NONE;
    }
    return controller->current_operation;
}

/**
 * @brief 获取当前状态
 */
MediaState controller_get_state(MediaController *controller) {
    if (controller == NULL) {
        return MEDIA_STATE_NULL;
    }
    return controller->state;
}

/**
 * @brief 枚举视频设备
 */
int controller_enum_video_devices(char devices[][256], int max_count) {
    return recorder_enum_video_devices(devices, max_count);
}

/**
 * @brief 枚举音频设备
 */
int controller_enum_audio_devices(char devices[][256], int max_count) {
    return recorder_enum_audio_devices(devices, max_count);
}
