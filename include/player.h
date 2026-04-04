/**
 * @file player.h
 * @brief 音视频播放模块头文件
 * @details 提供基于 GStreamer 的音视频播放功能
 */

#ifndef PLAYER_H
#define PLAYER_H

#include "media_types.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 播放器结构体（不透明指针）
 */
typedef struct MediaPlayer MediaPlayer;

/**
 * @brief 播放器配置结构体
 */
typedef struct {
    char video_sink[64];        /**< 视频输出插件 (如 xvimagesink, glimagesink) */
    char audio_sink[64];        /**< 音频输出插件 (如 autoaudiosink, alsasink) */
    int low_latency;            /**< 低延迟模式 */
    int buffer_duration;        /**< 缓冲时长 (毫秒) */
    int buffer_size;            /**< 缓冲区大小 (字节) */
    int enable_hardware_decode; /**< 启用硬件解码 */
    int sync;                   /**< 音视频同步 */
    int64_t position_update_interval; /**< 位置更新间隔 (毫秒) */
} PlayerConfig;

/**
 * @brief 播放器状态回调函数类型
 */
typedef void (*PlayerStateCallback)(MediaPlayer *player, MediaState state, void *user_data);

/**
 * @brief 播放器位置回调函数类型
 */
typedef void (*PlayerPositionCallback)(MediaPlayer *player, int64_t position, void *user_data);

/**
 * @brief 播放器错误回调函数类型
 */
typedef void (*PlayerErrorCallback)(MediaPlayer *player, MediaErrorCode code, 
                                    const char *message, void *user_data);

/**
 * @brief 播放器回调结构体
 */
typedef struct {
    PlayerStateCallback on_state_changed;   /**< 状态改变回调 */
    PlayerPositionCallback on_position;     /**< 位置更新回调 */
    PlayerErrorCallback on_error;           /**< 错误回调 */
    MediaEventCallback on_event;            /**< 事件回调 */
    void *user_data;                        /**< 用户数据 */
} PlayerCallbacks;

/**
 * @brief 创建播放器
 * @param config 播放器配置
 * @return 播放器指针，失败返回NULL
 */
MediaPlayer *player_create(const PlayerConfig *config);

/**
 * @brief 销毁播放器
 * @param player 播放器指针
 */
void player_destroy(MediaPlayer *player);

/**
 * @brief 设置播放器回调
 * @param player 播放器指针
 * @param callbacks 回调结构体
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_callbacks(MediaPlayer *player, const PlayerCallbacks *callbacks);

/**
 * @brief 打开媒体文件
 * @param player 播放器指针
 * @param uri 媒体URI (文件路径或网络URL)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_open(MediaPlayer *player, const char *uri);

/**
 * @brief 关闭媒体文件
 * @param player 播放器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_close(MediaPlayer *player);

/**
 * @brief 开始播放
 * @param player 播放器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_play(MediaPlayer *player);

/**
 * @brief 暂停播放
 * @param player 播放器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_pause(MediaPlayer *player);

/**
 * @brief 停止播放
 * @param player 播放器指针
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_stop(MediaPlayer *player);

/**
 * @brief 定位播放
 * @param player 播放器指针
 * @param position 目标位置 (纳秒)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_seek(MediaPlayer *player, int64_t position);

/**
 * @brief 获取当前播放位置
 * @param player 播放器指针
 * @return 当前位置 (纳秒)
 */
int64_t player_get_position(MediaPlayer *player);

/**
 * @brief 获取媒体时长
 * @param player 播放器指针
 * @return 时长 (纳秒)
 */
int64_t player_get_duration(MediaPlayer *player);

/**
 * @brief 获取当前播放状态
 * @param player 播放器指针
 * @return 播放状态
 */
MediaState player_get_state(MediaPlayer *player);

/**
 * @brief 获取媒体信息
 * @param player 播放器指针
 * @param info 媒体信息输出
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_get_media_info(MediaPlayer *player, MediaInfo *info);

/**
 * @brief 设置音量
 * @param player 播放器指针
 * @param volume 音量 (0.0 - 1.0)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_volume(MediaPlayer *player, double volume);

/**
 * @brief 获取音量
 * @param player 播放器指针
 * @return 音量 (0.0 - 1.0)
 */
double player_get_volume(MediaPlayer *player);

/**
 * @brief 设置静音
 * @param player 播放器指针
 * @param mute 是否静音
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_mute(MediaPlayer *player, int mute);

/**
 * @brief 获取静音状态
 * @param player 播放器指针
 * @return 是否静音
 */
int player_get_mute(MediaPlayer *player);

/**
 * @brief 设置播放速度
 * @param player 播放器指针
 * @param rate 播放速度 (0.5, 1.0, 2.0 等)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_rate(MediaPlayer *player, double rate);

/**
 * @brief 获取播放速度
 * @param player 播放器指针
 * @return 播放速度
 */
double player_get_rate(MediaPlayer *player);

/**
 * @brief 设置视频输出窗口
 * @param player 播放器指针
 * @param window_id 窗口ID (X11 Window ID)
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_window(MediaPlayer *player, uintptr_t window_id);

/**
 * @brief 设置视频渲染区域
 * @param player 播放器指针
 * @param rect 渲染区域
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_set_render_rect(MediaPlayer *player, const Rect *rect);

/**
 * @brief 获取当前视频帧
 * @param player 播放器指针
 * @param buffer 输出缓冲区
 * @param width 输出宽度
 * @param height 输出高度
 * @return 成功返回 MEDIA_OK
 */
MediaErrorCode player_get_frame(MediaPlayer *player, uint8_t *buffer, 
                                int width, int height);

/**
 * @brief 初始化播放器配置为默认值
 * @param config 配置结构体指针
 */
void player_config_init(PlayerConfig *config);

/**
 * @brief 初始化播放器回调为默认值
 * @param callbacks 回调结构体指针
 */
void player_callbacks_init(PlayerCallbacks *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_H */
