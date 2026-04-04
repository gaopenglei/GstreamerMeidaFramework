/**
 * @file transcoder.h
 * @brief 音视频转码模块头文件
 * @details 提供基于 GStreamer 的音视频转码功能
 */

#ifndef TRANSCODER_H
#define TRANSCODER_H

#include "media_types.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 转码器结构体（不透明指针）
 */
typedef struct MediaTranscoder MediaTranscoder;

/**
 * @brief 转码配置结构体
 */
typedef struct {
    /* 输入配置 */
    char input_file[512];               /**< 输入文件路径 */
    
    /* 输出配置 */
    char output_file[512];              /**< 输出文件路径 */
    ContainerFormat output_container;   /**< 输出容器格式 */
    
    /* 视频转码配置 */
    int video_transcode;                /**< 是否转码视频 */
    VideoParams video_params;           /**< 视频参数 */
    
    /* 音频转码配置 */
    int audio_transcode;                /**< 是否转码音频 */
    AudioParams audio_params;           /**< 音频参数 */
    
    /* 性能配置 */
    int low_latency;                    /**< 低延迟模式 */
    int enable_hardware;                /**< 启用硬件加速 */
    int threads;                        /**< 编码线程数 */
    int copy_ts;                        /**< 复制时间戳 */
    
    /* 转码范围 */
    int64_t start_time;                 /**< 开始时间 (纳秒) */
    int64_t end_time;                   /**< 结束时间 (纳秒) */
} TranscoderConfig;

/**
 * @brief 转码进度信息
 */
typedef struct {
    int64_t position;                   /**< 当前位置 (纳秒) */
    int64_t duration;                   /**< 总时长 (纳秒) */
    double progress;                    /**< 进度百分比 (0-100) */
    int64_t bytes_processed;            /**< 已处理字节数 */
    int64_t bytes_total;                /**< 总字节数 */
    double bitrate;                     /**< 当前码率 (bps) */
    double speed;                       /**< 转码速度 (倍速) */
    int64_t elapsed_time;               /**< 已用时间 (毫秒) */
    int64_t estimated_remaining;        /**< 预计剩余时间 (毫秒) */
} TranscodeProgress;

/**
 * @brief 转码器状态回调函数类型
 */
typedef void (*TranscoderStateCallback)(MediaTranscoder *transcoder, MediaState state, void *user_data);

/**
 * @brief 转码器进度回调函数类型
 */
typedef void (*TranscoderProgressCallback)(MediaTranscoder *transcoder, const TranscodeProgress *progress, void *user_data);

/**
 * @brief 转码器错误回调函数类型
 */
typedef void (*TranscoderErrorCallback)(MediaTranscoder *transcoder, MediaErrorCode code,
                                        const char *message, void *user_data);

/**
 * @brief 转码器回调结构体
 */
typedef struct {
    TranscoderStateCallback on_state_changed;   /**< 状态改变回调 */
    TranscoderProgressCallback on_progress;     /**< 进度回调 */
    TranscoderErrorCallback on_error;           /**< 错误回调 */
    MediaEventCallback on_event;                /**< 事件回调 */
    void *user_data;                            /**< 用户数据 */
} TranscoderCallbacks;

/**
 * @brief 创建转码器
 * @param config 转码器配置
 * @return 转码器指针，失败返回NULL
 */
MediaTranscoder *transcoder_create(const TranscoderConfig *config);

/**
 * @brief 销毁转码器
 * @param transcoder 转码器指针
 */
void transcoder_destroy(MediaTranscoder *transcoder);

/**
 * @brief 设置转码器回调
 * @param transcoder 转码器指针
 * @param callbacks 回调结构体
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_set_callbacks(MediaTranscoder *transcoder, const TranscoderCallbacks *callbacks);

/**
 * @brief 开始转码
 * @param transcoder 转码器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_start(MediaTranscoder *transcoder);

/**
 * @brief 暂停转码
 * @param transcoder 转码器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_pause(MediaTranscoder *transcoder);

/**
 * @brief 恢复转码
 * @param transcoder 转码器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_resume(MediaTranscoder *transcoder);

/**
 * @brief 停止转码
 * @param transcoder 转码器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_stop(MediaTranscoder *transcoder);

/**
 * @brief 获取当前转码状态
 * @param transcoder 转码器指针
 * @return 转码状态
 */
MediaState transcoder_get_state(MediaTranscoder *transcoder);

/**
 * @brief 获取转码进度
 * @param transcoder 转码器指针
 * @param progress 进度信息输出
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_get_progress(MediaTranscoder *transcoder, TranscodeProgress *progress);

/**
 * @brief 获取输入文件信息
 * @param transcoder 转码器指针
 * @param info 媒体信息输出
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_get_input_info(MediaTranscoder *transcoder, MediaInfo *info);

/**
 * @brief 设置视频转码参数
 * @param transcoder 转码器指针
 * @param params 视频参数
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_set_video_params(MediaTranscoder *transcoder, const VideoParams *params);

/**
 * @brief 设置音频转码参数
 * @param transcoder 转码器指针
 * @param params 音频参数
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_set_audio_params(MediaTranscoder *transcoder, const AudioParams *params);

/**
 * @brief 设置转码范围
 * @param transcoder 转码器指针
 * @param start_time 开始时间 (纳秒)
 * @param end_time 结束时间 (纳秒)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode transcoder_set_range(MediaTranscoder *transcoder, int64_t start_time, int64_t end_time);

/**
 * @brief 初始化转码器配置为默认值
 * @param config 配置结构体指针
 */
void transcoder_config_init(TranscoderConfig *config);

/**
 * @brief 初始化转码器回调为默认值
 * @param callbacks 回调结构体指针
 */
void transcoder_callbacks_init(TranscoderCallbacks *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* TRANSCODER_H */
