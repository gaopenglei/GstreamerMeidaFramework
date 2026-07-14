/**
 * @file media_types.c
 * @brief 媒体类型定义实现
 */

#include "media_types.h"
#include <string.h>
#include <strings.h>

/**
 * @brief 获取视频编解码格式字符串
 */
const char *video_codec_to_string(VideoCodec codec) {
    switch (codec) {
        case VIDEO_CODEC_NONE:
            return "none";
        case VIDEO_CODEC_H264:
            return "h264";
        case VIDEO_CODEC_H265:
            return "h265";
        case VIDEO_CODEC_VP8:
            return "vp8";
        case VIDEO_CODEC_VP9:
            return "vp9";
        case VIDEO_CODEC_AV1:
            return "av1";
        case VIDEO_CODEC_MPEG2:
            return "mpeg2";
        case VIDEO_CODEC_MPEG4:
            return "mpeg4";
        default:
            return "unknown";
    }
}

/**
 * @brief 获取音频编解码格式字符串
 */
const char *audio_codec_to_string(AudioCodec codec) {
    switch (codec) {
        case AUDIO_CODEC_NONE:
            return "none";
        case AUDIO_CODEC_AAC:
            return "aac";
        case AUDIO_CODEC_OPUS:
            return "opus";
        case AUDIO_CODEC_MP3:
            return "mp3";
        case AUDIO_CODEC_VORBIS:
            return "vorbis";
        case AUDIO_CODEC_FLAC:
            return "flac";
        case AUDIO_CODEC_PCM:
            return "pcm";
        case AUDIO_CODEC_G711:
            return "g711";
        default:
            return "unknown";
    }
}

/**
 * @brief 获取容器格式字符串
 */
const char *container_format_to_string(ContainerFormat format) {
    switch (format) {
        case CONTAINER_NONE:
            return "none";
        case CONTAINER_MP4:
            return "mp4";
        case CONTAINER_MKV:
            return "mkv";
        case CONTAINER_WEBM:
            return "webm";
        case CONTAINER_AVI:
            return "avi";
        case CONTAINER_TS:
            return "ts";
        case CONTAINER_FLV:
            return "flv";
        default:
            return "unknown";
    }
}

/**
 * @brief 获取像素格式字符串
 */
const char *pixel_format_to_string(PixelFormat format) {
    switch (format) {
        case PIXEL_FORMAT_NONE:
            return "none";
        case PIXEL_FORMAT_I420:
            return "i420";
        case PIXEL_FORMAT_NV12:
            return "nv12";
        case PIXEL_FORMAT_YUY2:
            return "yuy2";
        case PIXEL_FORMAT_UYVY:
            return "uyvy";
        case PIXEL_FORMAT_RGB24:
            return "rgb24";
        case PIXEL_FORMAT_RGB32:
            return "rgb32";
        case PIXEL_FORMAT_BGR24:
            return "bgr24";
        case PIXEL_FORMAT_BGR32:
            return "bgr32";
        default:
            return "unknown";
    }
}

/**
 * @brief 获取媒体状态字符串
 */
const char *media_state_to_string(MediaState state) {
    switch (state) {
        case MEDIA_STATE_NULL:
            return "null";
        case MEDIA_STATE_READY:
            return "ready";
        case MEDIA_STATE_PAUSED:
            return "paused";
        case MEDIA_STATE_PLAYING:
            return "playing";
        case MEDIA_STATE_ERROR:
            return "error";
        case MEDIA_STATE_EOS:
            return "eos";
        default:
            return "unknown";
    }
}

/**
 * @brief 从字符串解析视频编解码格式
 */
VideoCodec video_codec_from_string(const char *str) {
    if (str == NULL) {
        return VIDEO_CODEC_NONE;
    }
    
    if (strcasecmp(str, "h264") == 0 || strcasecmp(str, "avc") == 0) {
        return VIDEO_CODEC_H264;
    } else if (strcasecmp(str, "h265") == 0 || strcasecmp(str, "hevc") == 0) {
        return VIDEO_CODEC_H265;
    } else if (strcasecmp(str, "vp8") == 0) {
        return VIDEO_CODEC_VP8;
    } else if (strcasecmp(str, "vp9") == 0) {
        return VIDEO_CODEC_VP9;
    } else if (strcasecmp(str, "av1") == 0) {
        return VIDEO_CODEC_AV1;
    } else if (strcasecmp(str, "mpeg2") == 0) {
        return VIDEO_CODEC_MPEG2;
    } else if (strcasecmp(str, "mpeg4") == 0) {
        return VIDEO_CODEC_MPEG4;
    }
    
    return VIDEO_CODEC_NONE;
}

/**
 * @brief 从字符串解析音频编解码格式
 */
AudioCodec audio_codec_from_string(const char *str) {
    if (str == NULL) {
        return AUDIO_CODEC_NONE;
    }
    
    if (strcasecmp(str, "aac") == 0) {
        return AUDIO_CODEC_AAC;
    } else if (strcasecmp(str, "opus") == 0) {
        return AUDIO_CODEC_OPUS;
    } else if (strcasecmp(str, "mp3") == 0 || strcasecmp(str, "mpeg") == 0) {
        return AUDIO_CODEC_MP3;
    } else if (strcasecmp(str, "vorbis") == 0) {
        return AUDIO_CODEC_VORBIS;
    } else if (strcasecmp(str, "flac") == 0) {
        return AUDIO_CODEC_FLAC;
    } else if (strcasecmp(str, "pcm") == 0) {
        return AUDIO_CODEC_PCM;
    } else if (strcasecmp(str, "g711") == 0) {
        return AUDIO_CODEC_G711;
    }
    
    return AUDIO_CODEC_NONE;
}

/**
 * @brief 初始化视频参数为默认值
 */
void video_params_init(VideoParams *params) {
    if (params == NULL) {
        return;
    }
    
    memset(params, 0, sizeof(VideoParams));
    params->width = 1920;
    params->height = 1080;
    params->framerate_num = 30;
    params->framerate_den = 1;
    params->pixel_format = PIXEL_FORMAT_I420;
    params->codec = VIDEO_CODEC_H264;
    params->bitrate = 4000000;  /* 4 Mbps */
    params->gop_size = 30;
    params->profile = 0;
    params->level = 0;
}

/**
 * @brief 初始化音频参数为默认值
 */
void audio_params_init(AudioParams *params) {
    if (params == NULL) {
        return;
    }
    
    memset(params, 0, sizeof(AudioParams));
    params->sample_rate = 44100;
    params->channels = 2;
    params->sample_format = SAMPLE_FORMAT_S16;
    params->codec = AUDIO_CODEC_AAC;
    params->bitrate = 128000;  /* 128 kbps */
    params->frame_size = 1024;
}

/**
 * @brief 初始化媒体信息为默认值
 */
void media_info_init(MediaInfo *info) {
    if (info == NULL) {
        return;
    }
    
    memset(info, 0, sizeof(MediaInfo));
    video_params_init(&info->video_params);
    audio_params_init(&info->audio_params);
}
