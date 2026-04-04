/**
 * @file error.c
 * @brief 错误处理模块实现
 */

#include "error.h"
#include "logger.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* 全局错误上下文 */
static ErrorContext g_error_ctx = {0};
static pthread_mutex_t g_error_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 获取错误码描述
 */
const char *error_code_to_string(MediaErrorCode code) {
    switch (code) {
        /* 成功 */
        case MEDIA_OK:
            return "Success";
            
        /* 通用错误 */
        case MEDIA_ERROR_UNKNOWN:
            return "Unknown error";
        case MEDIA_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case MEDIA_ERROR_NO_MEMORY:
            return "Out of memory";
        case MEDIA_ERROR_NOT_INITIALIZED:
            return "Not initialized";
        case MEDIA_ERROR_ALREADY_INITIALIZED:
            return "Already initialized";
        case MEDIA_ERROR_TIMEOUT:
            return "Timeout";
        case MEDIA_ERROR_NOT_SUPPORTED:
            return "Operation not supported";
        case MEDIA_ERROR_BUSY:
            return "Resource busy";
        case MEDIA_ERROR_NOT_FOUND:
            return "Resource not found";
        case MEDIA_ERROR_PERMISSION_DENIED:
            return "Permission denied";
            
        /* 播放模块错误 */
        case MEDIA_ERROR_PLAYER_INIT_FAILED:
            return "Player initialization failed";
        case MEDIA_ERROR_PLAYER_CREATE_PIPELINE:
            return "Failed to create player pipeline";
        case MEDIA_ERROR_PLAYER_INVALID_URI:
            return "Invalid URI";
        case MEDIA_ERROR_PLAYER_NO_DECODER:
            return "No decoder available";
        case MEDIA_ERROR_PLAYER_NO_SINK:
            return "No output sink available";
        case MEDIA_ERROR_PLAYER_STATE_CHANGE:
            return "State change failed";
        case MEDIA_ERROR_PLAYER_EOS:
            return "End of stream";
        case MEDIA_ERROR_PLAYER_BUFFER_UNDERFLOW:
            return "Buffer underflow";
        case MEDIA_ERROR_PLAYER_SYNC_ERROR:
            return "Synchronization error";
            
        /* 录制模块错误 */
        case MEDIA_ERROR_RECORDER_INIT_FAILED:
            return "Recorder initialization failed";
        case MEDIA_ERROR_RECORDER_CREATE_PIPELINE:
            return "Failed to create recorder pipeline";
        case MEDIA_ERROR_RECORDER_NO_DEVICE:
            return "Device not found";
        case MEDIA_ERROR_RECORDER_DEVICE_BUSY:
            return "Device busy";
        case MEDIA_ERROR_RECORDER_NO_ENCODER:
            return "No encoder available";
        case MEDIA_ERROR_RECORDER_MUX_ERROR:
            return "Muxing error";
        case MEDIA_ERROR_RECORDER_FILE_ERROR:
            return "File error";
        case MEDIA_ERROR_RECORDER_BUFFER_OVERFLOW:
            return "Buffer overflow";
            
        /* 转码模块错误 */
        case MEDIA_ERROR_TRANSCODER_INIT_FAILED:
            return "Transcoder initialization failed";
        case MEDIA_ERROR_TRANSCODER_CREATE_PIPELINE:
            return "Failed to create transcoder pipeline";
        case MEDIA_ERROR_TRANSCODER_INVALID_INPUT:
            return "Invalid input file";
        case MEDIA_ERROR_TRANSCODER_INVALID_OUTPUT:
            return "Invalid output format";
        case MEDIA_ERROR_TRANSCODER_NO_DECODER:
            return "No decoder available";
        case MEDIA_ERROR_TRANSCODER_NO_ENCODER:
            return "No encoder available";
        case MEDIA_ERROR_TRANSCODER_FORMAT_ERROR:
            return "Format error";
            
        /* 编解码模块错误 */
        case MEDIA_ERROR_CODEC_INIT_FAILED:
            return "Codec initialization failed";
        case MEDIA_ERROR_CODEC_ENCODE_FAILED:
            return "Encoding failed";
        case MEDIA_ERROR_CODEC_DECODE_FAILED:
            return "Decoding failed";
        case MEDIA_ERROR_CODEC_NOT_SUPPORTED:
            return "Codec not supported";
        case MEDIA_ERROR_CODEC_PARAM_ERROR:
            return "Codec parameter error";
        case MEDIA_ERROR_CODEC_RESOURCE_ERROR:
            return "Codec resource error";
            
        /* 输入输出模块错误 */
        case MEDIA_ERROR_IO_OPEN_FAILED:
            return "Failed to open";
        case MEDIA_ERROR_IO_READ_FAILED:
            return "Read failed";
        case MEDIA_ERROR_IO_WRITE_FAILED:
            return "Write failed";
        case MEDIA_ERROR_IO_CLOSE_FAILED:
            return "Close failed";
        case MEDIA_ERROR_IO_SEEK_FAILED:
            return "Seek failed";
        case MEDIA_ERROR_IO_NETWORK_ERROR:
            return "Network error";
            
        /* GStreamer 特定错误 */
        case MEDIA_ERROR_GST_INIT_FAILED:
            return "GStreamer initialization failed";
        case MEDIA_ERROR_GST_ELEMENT_CREATE:
            return "Failed to create GStreamer element";
        case MEDIA_ERROR_GST_ELEMENT_LINK:
            return "Failed to link GStreamer elements";
        case MEDIA_ERROR_GST_PIPELINE_ERROR:
            return "GStreamer pipeline error";
        case MEDIA_ERROR_GST_STATE_CHANGE:
            return "GStreamer state change error";
        case MEDIA_ERROR_GST_MESSAGE_ERROR:
            return "GStreamer message error";
        case MEDIA_ERROR_GST_PLUGIN_MISSING:
            return "GStreamer plugin missing";
            
        default:
            return "Unknown error code";
    }
}

/**
 * @brief 获取错误严重程度描述
 */
const char *error_severity_to_string(ErrorSeverity severity) {
    switch (severity) {
        case ERROR_SEVERITY_LOW:
            return "LOW";
        case ERROR_SEVERITY_MEDIUM:
            return "MEDIUM";
        case ERROR_SEVERITY_HIGH:
            return "HIGH";
        case ERROR_SEVERITY_CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief 根据错误码获取严重程度
 */
static ErrorSeverity error_get_severity(MediaErrorCode code) {
    switch (code) {
        case MEDIA_OK:
            return ERROR_SEVERITY_LOW;
            
        case MEDIA_ERROR_TIMEOUT:
        case MEDIA_ERROR_BUSY:
        case MEDIA_ERROR_PLAYER_BUFFER_UNDERFLOW:
        case MEDIA_ERROR_RECORDER_BUFFER_OVERFLOW:
            return ERROR_SEVERITY_LOW;
            
        case MEDIA_ERROR_INVALID_PARAM:
        case MEDIA_ERROR_NOT_FOUND:
        case MEDIA_ERROR_PLAYER_INVALID_URI:
        case MEDIA_ERROR_PLAYER_EOS:
        case MEDIA_ERROR_TRANSCODER_INVALID_INPUT:
        case MEDIA_ERROR_TRANSCODER_INVALID_OUTPUT:
            return ERROR_SEVERITY_MEDIUM;
            
        case MEDIA_ERROR_NO_MEMORY:
        case MEDIA_ERROR_PLAYER_NO_DECODER:
        case MEDIA_ERROR_PLAYER_NO_SINK:
        case MEDIA_ERROR_PLAYER_SYNC_ERROR:
        case MEDIA_ERROR_RECORDER_NO_DEVICE:
        case MEDIA_ERROR_RECORDER_NO_ENCODER:
        case MEDIA_ERROR_TRANSCODER_NO_DECODER:
        case MEDIA_ERROR_TRANSCODER_NO_ENCODER:
        case MEDIA_ERROR_CODEC_NOT_SUPPORTED:
        case MEDIA_ERROR_GST_PLUGIN_MISSING:
            return ERROR_SEVERITY_HIGH;
            
        case MEDIA_ERROR_PLAYER_INIT_FAILED:
        case MEDIA_ERROR_PLAYER_CREATE_PIPELINE:
        case MEDIA_ERROR_RECORDER_INIT_FAILED:
        case MEDIA_ERROR_RECORDER_CREATE_PIPELINE:
        case MEDIA_ERROR_TRANSCODER_INIT_FAILED:
        case MEDIA_ERROR_TRANSCODER_CREATE_PIPELINE:
        case MEDIA_ERROR_CODEC_INIT_FAILED:
        case MEDIA_ERROR_GST_INIT_FAILED:
        case MEDIA_ERROR_GST_PIPELINE_ERROR:
            return ERROR_SEVERITY_CRITICAL;
            
        default:
            return ERROR_SEVERITY_MEDIUM;
    }
}

/**
 * @brief 初始化错误处理模块
 */
int error_init(void) {
    pthread_mutex_lock(&g_error_mutex);
    
    memset(&g_error_ctx, 0, sizeof(ErrorContext));
    g_error_ctx.error_count = 0;
    
    pthread_mutex_unlock(&g_error_mutex);
    
    LOG_INFO("Error handling module initialized");
    return 0;
}

/**
 * @brief 关闭错误处理模块
 */
void error_shutdown(void) {
    pthread_mutex_lock(&g_error_mutex);
    
    memset(&g_error_ctx, 0, sizeof(ErrorContext));
    
    pthread_mutex_unlock(&g_error_mutex);
    
    LOG_INFO("Error handling module shutdown");
}

/**
 * @brief 设置错误回调函数
 */
void error_set_callback(ErrorCallback callback, void *user_data) {
    pthread_mutex_lock(&g_error_mutex);
    
    g_error_ctx.callback = callback;
    g_error_ctx.user_data = user_data;
    
    pthread_mutex_unlock(&g_error_mutex);
}

/**
 * @brief 设置错误
 */
void error_set(MediaErrorCode code, const char *file, int line,
               const char *func, const char *fmt, ...) {
    pthread_mutex_lock(&g_error_mutex);
    
    /* 设置错误信息 */
    ErrorInfo *error = &g_error_ctx.last_error;
    error->code = code;
    error->severity = error_get_severity(code);
    error->line = line;
    error->timestamp = (uint64_t)time(NULL);
    
    /* 设置文件名 */
    if (file != NULL) {
        const char *filename = strrchr(file, '/');
        filename = (filename != NULL) ? filename + 1 : file;
        strncpy(error->file, filename, sizeof(error->file) - 1);
    }
    
    /* 设置函数名 */
    if (func != NULL) {
        strncpy(error->function, func, sizeof(error->function) - 1);
    }
    
    /* 格式化错误消息 */
    if (fmt != NULL) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
    } else {
        strncpy(error->message, error_code_to_string(code), 
                sizeof(error->message) - 1);
    }
    
    /* 增加错误计数 */
    g_error_ctx.error_count++;
    
    /* 记录日志 */
    LOG_ERROR("[%s] %s (code: %d, severity: %s)",
              error->function,
              error->message,
              error->code,
              error_severity_to_string(error->severity));
    
    /* 调用回调函数 */
    if (g_error_ctx.callback != NULL) {
        g_error_ctx.callback(error, g_error_ctx.user_data);
    }
    
    pthread_mutex_unlock(&g_error_mutex);
}

/**
 * @brief 获取最后一个错误
 */
const ErrorInfo *error_get_last(void) {
    return &g_error_ctx.last_error;
}

/**
 * @brief 清除错误
 */
void error_clear(void) {
    pthread_mutex_lock(&g_error_mutex);
    
    memset(&g_error_ctx.last_error, 0, sizeof(ErrorInfo));
    g_error_ctx.error_count = 0;
    
    pthread_mutex_unlock(&g_error_mutex);
}
