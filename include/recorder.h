/**
 * @file recorder.h
 * @brief 音视频录制模块头文件
 * @details 提供基于 GStreamer 的音视频录制功能
 */

#ifndef RECORDER_H
#define RECORDER_H

#include "media_types.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 录制器结构体（不透明指针）
 */
typedef struct MediaRecorder MediaRecorder;

/**
 * @brief 视频源类型
 */
typedef enum {
    VIDEO_SOURCE_NONE = 0,      /**< 无视频源 */
    VIDEO_SOURCE_V4L2 = 1,      /**< V4L2 摄像头 */
    VIDEO_SOURCE_TEST = 2,      /**< 测试模式 */
    VIDEO_SOURCE_RTSP = 3,      /**< RTSP 网络流 */
    VIDEO_SOURCE_FILE = 4       /**< 文件输入 */
} VideoSourceType;

/**
 * @brief 音频源类型
 */
typedef enum {
    AUDIO_SOURCE_NONE = 0,      /**< 无音频源 */
    AUDIO_SOURCE_ALSA = 1,      /**< ALSA 设备 */
    AUDIO_SOURCE_PULSE = 2,     /**< PulseAudio */
    AUDIO_SOURCE_TEST = 3,      /**< 测试模式 */
    AUDIO_SOURCE_RTSP = 4,      /**< RTSP 网络流 */
    AUDIO_SOURCE_FILE = 5       /**< 文件输入 */
} AudioSourceType;

/**
 * @brief 录制器配置结构体
 */
typedef struct {
    /* 视频源配置 */
    VideoSourceType video_source;       /**< 视频源类型 */
    char video_device[256];             /**< 视频设备路径 */
    VideoParams video_params;           /**< 视频参数 */
    
    /* 音频源配置 */
    AudioSourceType audio_source;       /**< 音频源类型 */
    char audio_device[256];             /**< 音频设备路径 */
    AudioParams audio_params;           /**< 音频参数 */
    
    /* 输出配置 */
    char output_file[512];              /**< 输出文件路径 */
    ContainerFormat container;          /**< 容器格式 */
    
    /* 性能配置 */
    int low_latency;                    /**< 低延迟模式 */
    int buffer_size;                    /**< 缓冲区大小 */
    int enable_hardware_encode;         /**< 启用硬件编码 */
    int realtime;                       /**< 实时录制模式 */
    
    /* 录制限制 */
    int64_t max_duration;               /**< 最大录制时长 (纳秒) */
    int64_t max_size;                   /**< 最大文件大小 (字节) */
} RecorderConfig;

/**
 * @brief 录制器状态回调函数类型
 */
typedef void (*RecorderStateCallback)(MediaRecorder *recorder, MediaState state, void *user_data);

/**
 * @brief 录制器错误回调函数类型
 */
typedef void (*RecorderErrorCallback)(MediaRecorder *recorder, MediaErrorCode code,
                                      const char *message, void *user_data);

/**
 * @brief 录制器进度回调函数类型
 */
typedef void (*RecorderProgressCallback)(MediaRecorder *recorder, int64_t duration,
                                         int64_t size, void *user_data);

/**
 * @brief 录制器回调结构体
 */
typedef struct {
    RecorderStateCallback on_state_changed;     /**< 状态改变回调 */
    RecorderErrorCallback on_error;             /**< 错误回调 */
    RecorderProgressCallback on_progress;       /**< 进度回调 */
    MediaEventCallback on_event;                /**< 事件回调 */
    void *user_data;                            /**< 用户数据 */
} RecorderCallbacks;

/**
 * @brief 创建录制器
 * @param config 录制器配置
 * @return 录制器指针，失败返回NULL
 */
MediaRecorder *recorder_create(const RecorderConfig *config);

/**
 * @brief 销毁录制器
 * @param recorder 录制器指针
 */
void recorder_destroy(MediaRecorder *recorder);

/**
 * @brief 设置录制器回调
 * @param recorder 录制器指针
 * @param callbacks 回调结构体
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_set_callbacks(MediaRecorder *recorder, const RecorderCallbacks *callbacks);

/**
 * @brief 开始录制
 * @param recorder 录制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_start(MediaRecorder *recorder);

/**
 * @brief 暂停录制
 * @param recorder 录制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_pause(MediaRecorder *recorder);

/**
 * @brief 恢复录制
 * @param recorder 录制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_resume(MediaRecorder *recorder);

/**
 * @brief 停止录制
 * @param recorder 录制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_stop(MediaRecorder *recorder);

/**
 * @brief 获取当前录制状态
 * @param recorder 录制器指针
 * @return 录制状态
 */
MediaState recorder_get_state(MediaRecorder *recorder);

/**
 * @brief 获取录制时长
 * @param recorder 录制器指针
 * @return 录制时长 (纳秒)
 */
int64_t recorder_get_duration(MediaRecorder *recorder);

/**
 * @brief 获取录制文件大小
 * @param recorder 录制器指针
 * @return 文件大小 (字节)
 */
int64_t recorder_get_file_size(MediaRecorder *recorder);

/**
 * @brief 设置视频参数
 * @param recorder 录制器指针
 * @param params 视频参数
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_set_video_params(MediaRecorder *recorder, const VideoParams *params);

/**
 * @brief 设置音频参数
 * @param recorder 录制器指针
 * @param params 音频参数
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_set_audio_params(MediaRecorder *recorder, const AudioParams *params);

/**
 * @brief 设置输出文件
 * @param recorder 录制器指针
 * @param file_path 输出文件路径
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode recorder_set_output_file(MediaRecorder *recorder, const char *file_path);

/**
 * @brief 枚举可用视频设备
 * @param devices 设备列表输出
 * @param max_count 最大设备数
 * @return 实际设备数
 */
int recorder_enum_video_devices(char devices[][256], int max_count);

/**
 * @brief 枚举可用音频设备
 * @param devices 设备列表输出
 * @param max_count 最大设备数
 * @return 实际设备数
 */
int recorder_enum_audio_devices(char devices[][256], int max_count);

/**
 * @brief 初始化录制器配置为默认值
 * @param config 配置结构体指针
 */
void recorder_config_init(RecorderConfig *config);

/**
 * @brief 初始化录制器回调为默认值
 * @param callbacks 回调结构体指针
 */
void recorder_callbacks_init(RecorderCallbacks *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_H */
