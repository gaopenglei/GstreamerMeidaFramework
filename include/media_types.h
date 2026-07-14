/**
 * @file media_types.h
 * @brief 媒体类型定义头文件
 * @details 定义媒体框架使用的各种类型、枚举和结构体
 */

#ifndef MEDIA_TYPES_H
#define MEDIA_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 视频编解码格式
 */
typedef enum {
    VIDEO_CODEC_NONE = 0,       /**< 无 */
    VIDEO_CODEC_H264 = 1,       /**< H.264/AVC */
    VIDEO_CODEC_H265 = 2,       /**< H.265/HEVC */
    VIDEO_CODEC_VP8 = 3,        /**< VP8 */
    VIDEO_CODEC_VP9 = 4,        /**< VP9 */
    VIDEO_CODEC_AV1 = 5,        /**< AV1 */
    VIDEO_CODEC_MPEG2 = 6,      /**< MPEG-2 */
    VIDEO_CODEC_MPEG4 = 7       /**< MPEG-4 */
} VideoCodec;

/**
 * @brief 音频编解码格式
 */
typedef enum {
    AUDIO_CODEC_NONE = 0,       /**< 无 */
    AUDIO_CODEC_AAC = 1,        /**< AAC */
    AUDIO_CODEC_OPUS = 2,       /**< Opus */
    AUDIO_CODEC_MP3 = 3,        /**< MP3 */
    AUDIO_CODEC_VORBIS = 4,     /**< Vorbis */
    AUDIO_CODEC_FLAC = 5,       /**< FLAC */
    AUDIO_CODEC_PCM = 6,        /**< PCM */
    AUDIO_CODEC_G711 = 7        /**< G.711 */
} AudioCodec;

/**
 * @brief 容器格式
 */
typedef enum {
    CONTAINER_NONE = 0,         /**< 无 */
    CONTAINER_MP4 = 1,          /**< MP4 */
    CONTAINER_MKV = 2,          /**< Matroska */
    CONTAINER_WEBM = 3,         /**< WebM */
    CONTAINER_AVI = 4,          /**< AVI */
    CONTAINER_TS = 5,           /**< MPEG-TS */
    CONTAINER_FLV = 6           /**< FLV */
} ContainerFormat;

/**
 * @brief 像素格式
 */
typedef enum {
    PIXEL_FORMAT_NONE = 0,      /**< 无 */
    PIXEL_FORMAT_I420 = 1,      /**< YUV 4:2:0 平面 */
    PIXEL_FORMAT_NV12 = 2,      /**< YUV 4:2:0 半平面 */
    PIXEL_FORMAT_YUY2 = 3,      /**< YUV 4:2:2 打包 */
    PIXEL_FORMAT_UYVY = 4,      /**< YUV 4:2:2 打包 */
    PIXEL_FORMAT_RGB24 = 5,     /**< RGB 24位 */
    PIXEL_FORMAT_RGB32 = 6,     /**< RGB 32位 */
    PIXEL_FORMAT_BGR24 = 7,     /**< BGR 24位 */
    PIXEL_FORMAT_BGR32 = 8      /**< BGR 32位 */
} PixelFormat;

/**
 * @brief 音频采样格式
 */
typedef enum {
    SAMPLE_FORMAT_NONE = 0,     /**< 无 */
    SAMPLE_FORMAT_U8 = 1,       /**< 8位无符号 */
    SAMPLE_FORMAT_S16 = 2,      /**< 16位有符号 */
    SAMPLE_FORMAT_S24 = 3,      /**< 24位有符号 */
    SAMPLE_FORMAT_S32 = 4,      /**< 32位有符号 */
    SAMPLE_FORMAT_F32 = 5,      /**< 32位浮点 */
    SAMPLE_FORMAT_F64 = 6       /**< 64位浮点 */
} SampleFormat;

/**
 * @brief 媒体状态
 */
typedef enum {
    MEDIA_STATE_NULL = 0,       /**< 空状态 */
    MEDIA_STATE_READY = 1,      /**< 就绪状态 */
    MEDIA_STATE_PAUSED = 2,     /**< 暂停状态 */
    MEDIA_STATE_PLAYING = 3,    /**< 播放状态 */
    MEDIA_STATE_ERROR = 4,      /**< 错误状态 */
    MEDIA_STATE_EOS = 5         /**< 媒体流结束 */
} MediaState;

/**
 * @brief 视频参数结构体
 */
typedef struct {
    int width;                  /**< 视频宽度 */
    int height;                 /**< 视频高度 */
    int framerate_num;          /**< 帧率分子 */
    int framerate_den;          /**< 帧率分母 */
    PixelFormat pixel_format;   /**< 像素格式 */
    VideoCodec codec;           /**< 视频编解码格式 */
    int bitrate;                /**< 比特率 (bps) */
    int gop_size;               /**< GOP大小 */
    int profile;                /**< 编码配置 */
    int level;                  /**< 编码级别 */
} VideoParams;

/**
 * @brief 音频参数结构体
 */
typedef struct {
    int sample_rate;            /**< 采样率 */
    int channels;               /**< 声道数 */
    SampleFormat sample_format; /**< 采样格式 */
    AudioCodec codec;           /**< 音频编解码格式 */
    int bitrate;                /**< 比特率 (bps) */
    int frame_size;             /**< 帧大小 */
} AudioParams;

/**
 * @brief 媒体信息结构体
 */
typedef struct {
    char uri[1024];             /**< 媒体URI */
    ContainerFormat container;  /**< 容器格式 */
    int64_t duration;           /**< 时长 (纳秒) */
    int64_t position;           /**< 当前位置 (纳秒) */
    int seekable;               /**< 是否可定位 */
    
    VideoParams video_params;   /**< 视频参数 */
    AudioParams audio_params;   /**< 音频参数 */
    
    int has_video;              /**< 是否有视频 */
    int has_audio;              /**< 是否有音频 */
} MediaInfo;

/**
 * @brief 矩形区域结构体
 */
typedef struct {
    int x;                      /**< X坐标 */
    int y;                      /**< Y坐标 */
    int width;                  /**< 宽度 */
    int height;                 /**< 高度 */
} Rect;

/**
 * @brief 大小结构体
 */
typedef struct {
    int width;                  /**< 宽度 */
    int height;                 /**< 高度 */
} Size;

/**
 * @brief 分数结构体
 */
typedef struct {
    int num;                    /**< 分子 */
    int den;                    /**< 分母 */
} Fraction;

/**
 * @brief 缓冲区结构体
 */
typedef struct {
    uint8_t *data;              /**< 数据指针 */
    size_t size;                /**< 数据大小 */
    size_t capacity;            /**< 缓冲区容量 */
    int64_t pts;                /**< 显示时间戳 */
    int64_t dts;                /**< 解码时间戳 */
    int64_t duration;           /**< 持续时间 */
    int flags;                  /**< 标志位 */
} MediaBuffer;

/**
 * @brief 媒体事件类型
 */
typedef enum {
    MEDIA_EVENT_EOS = 0,            /**< 流结束 */
    MEDIA_EVENT_ERROR = 1,          /**< 错误 */
    MEDIA_EVENT_STATE_CHANGED = 2,  /**< 状态改变 */
    MEDIA_EVENT_POSITION = 3,       /**< 位置更新 */
    MEDIA_EVENT_DURATION = 4,       /**< 时长更新 */
    MEDIA_EVENT_BUFFERING = 5,      /**< 缓冲中 */
    MEDIA_EVENT_SEEK_DONE = 6,      /**< 定位完成 */
    MEDIA_EVENT_TAG = 7,            /**< 标签信息 */
    MEDIA_EVENT_STREAM_START = 8    /**< 流开始 */
} MediaEventType;

/**
 * @brief 媒体事件结构体
 */
typedef struct {
    MediaEventType type;        /**< 事件类型 */
    int code;                   /**< 事件代码 */
    char message[512];          /**< 事件消息 */
    void *data;                 /**< 事件数据 */
    size_t data_size;           /**< 数据大小 */
} MediaEvent;

/**
 * @brief 媒体事件回调函数类型
 */
typedef void (*MediaEventCallback)(const MediaEvent *event, void *user_data);

/**
 * @brief 获取视频编解码格式字符串
 */
const char *video_codec_to_string(VideoCodec codec);

/**
 * @brief 获取音频编解码格式字符串
 */
const char *audio_codec_to_string(AudioCodec codec);

/**
 * @brief 获取容器格式字符串
 */
const char *container_format_to_string(ContainerFormat format);

/**
 * @brief 获取像素格式字符串
 */
const char *pixel_format_to_string(PixelFormat format);

/**
 * @brief 获取媒体状态字符串
 */
const char *media_state_to_string(MediaState state);

/**
 * @brief 从字符串解析视频编解码格式
 */
VideoCodec video_codec_from_string(const char *str);

/**
 * @brief 从字符串解析音频编解码格式
 */
AudioCodec audio_codec_from_string(const char *str);

/**
 * @brief 初始化视频参数为默认值
 */
void video_params_init(VideoParams *params);

/**
 * @brief 初始化音频参数为默认值
 */
void audio_params_init(AudioParams *params);

/**
 * @brief 初始化媒体信息为默认值
 */
void media_info_init(MediaInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_TYPES_H */
