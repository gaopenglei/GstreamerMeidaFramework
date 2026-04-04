/**
 * @file media_controller.h
 * @brief 媒体控制器头文件
 * @details 提供统一的媒体框架控制接口
 */

#ifndef MEDIA_CONTROLLER_H
#define MEDIA_CONTROLLER_H

#include "media_types.h"
#include "player.h"
#include "recorder.h"
#include "transcoder.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 媒体控制器结构体（不透明指针）
 */
typedef struct MediaController MediaController;

/**
 * @brief 控制器配置结构体
 */
typedef struct {
    char log_file[256];         /**< 日志文件路径 */
    LogLevel log_level;         /**< 日志级别 */
    int log_to_console;         /**< 是否输出到控制台 */
    int log_to_file;            /**< 是否输出到文件 */
    int enable_hardware;        /**< 启用硬件加速 */
    int low_latency;            /**< 低延迟模式 */
} ControllerConfig;

/**
 * @brief 操作类型
 */
typedef enum {
    OPERATION_NONE = 0,         /**< 无操作 */
    OPERATION_PLAY = 1,         /**< 播放 */
    OPERATION_RECORD = 2,       /**< 录制 */
    OPERATION_TRANSCODE = 3     /**< 转码 */
} OperationType;

/**
 * @brief 控制器状态回调函数类型
 */
typedef void (*ControllerStateCallback)(MediaController *controller, 
                                        OperationType operation,
                                        MediaState state, 
                                        void *user_data);

/**
 * @brief 控制器错误回调函数类型
 */
typedef void (*ControllerErrorCallback)(MediaController *controller,
                                        OperationType operation,
                                        MediaErrorCode code,
                                        const char *message,
                                        void *user_data);

/**
 * @brief 控制器回调结构体
 */
typedef struct {
    ControllerStateCallback on_state_changed;   /**< 状态改变回调 */
    ControllerErrorCallback on_error;           /**< 错误回调 */
    void *user_data;                            /**< 用户数据 */
} ControllerCallbacks;

/**
 * @brief 创建媒体控制器
 * @param config 控制器配置
 * @return 控制器指针，失败返回NULL
 */
MediaController *controller_create(const ControllerConfig *config);

/**
 * @brief 销毁媒体控制器
 * @param controller 控制器指针
 */
void controller_destroy(MediaController *controller);

/**
 * @brief 设置控制器回调
 * @param controller 控制器指针
 * @param callbacks 回调结构体
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_set_callbacks(MediaController *controller, 
                                        const ControllerCallbacks *callbacks);

/**
 * @brief 获取播放器实例
 * @param controller 控制器指针
 * @return 播放器指针
 */
MediaPlayer *controller_get_player(MediaController *controller);

/**
 * @brief 获取录制器实例
 * @param controller 控制器指针
 * @return 录制器指针
 */
MediaRecorder *controller_get_recorder(MediaController *controller);

/**
 * @brief 获取转码器实例
 * @param controller 控制器指针
 * @return 转码器指针
 */
MediaTranscoder *controller_get_transcoder(MediaController *controller);

/**
 * @brief 播放文件
 * @param controller 控制器指针
 * @param uri 文件URI或路径
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_play(MediaController *controller, const char *uri);

/**
 * @brief 暂停当前操作
 * @param controller 控制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_pause(MediaController *controller);

/**
 * @brief 恢复当前操作
 * @param controller 控制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_resume(MediaController *controller);

/**
 * @brief 停止当前操作
 * @param controller 控制器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_stop(MediaController *controller);

/**
 * @brief 开始录制
 * @param controller 控制器指针
 * @param config 录制配置
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_start_recording(MediaController *controller, 
                                          const RecorderConfig *config);

/**
 * @brief 开始转码
 * @param controller 控制器指针
 * @param config 转码配置
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode controller_start_transcoding(MediaController *controller,
                                            const TranscoderConfig *config);

/**
 * @brief 获取当前操作类型
 * @param controller 控制器指针
 * @return 操作类型
 */
OperationType controller_get_operation(MediaController *controller);

/**
 * @brief 获取当前状态
 * @param controller 控制器指针
 * @return 媒体状态
 */
MediaState controller_get_state(MediaController *controller);

/**
 * @brief 枚举视频设备
 * @param devices 设备列表输出
 * @param max_count 最大设备数
 * @return 实际设备数
 */
int controller_enum_video_devices(char devices[][256], int max_count);

/**
 * @brief 枚举音频设备
 * @param devices 设备列表输出
 * @param max_count 最大设备数
 * @return 实际设备数
 */
int controller_enum_audio_devices(char devices[][256], int max_count);

/**
 * @brief 初始化控制器配置为默认值
 * @param config 配置结构体指针
 */
void controller_config_init(ControllerConfig *config);

/**
 * @brief 初始化控制器回调为默认值
 * @param callbacks 回调结构体指针
 */
void controller_callbacks_init(ControllerCallbacks *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_CONTROLLER_H */
