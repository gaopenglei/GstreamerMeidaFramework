/**
 * @file main.c
 * @brief 媒体框架主程序入口
 * @details 提供命令行接口进行音视频播放、录制、转码操作
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <glib.h>

#include "media_controller.h"
#include "logger.h"

/* 全局控制器实例 */
static MediaController *g_controller = NULL;

/* 运行标志 */
static volatile int g_running = 1;

static void process_main_context(void) {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
}

/**
 * @brief 信号处理函数
 */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\nReceived signal, stopping...\n");
}

/**
 * @brief 控制器状态回调
 */
static void on_state_changed(MediaController *controller, 
                             OperationType operation,
                             MediaState state, 
                             void *user_data) {
    (void)controller;
    (void)user_data;
    
    const char *op_str = "";
    switch (operation) {
        case OPERATION_PLAY: op_str = "Play"; break;
        case OPERATION_RECORD: op_str = "Record"; break;
        case OPERATION_TRANSCODE: op_str = "Transcode"; break;
        default: op_str = "None"; break;
    }
    
    printf("[%s] State: %s\n", op_str, media_state_to_string(state));
    
    /* 如果是转码完成，退出程序 */
    if (operation == OPERATION_TRANSCODE && state == MEDIA_STATE_EOS) {
        g_running = 0;
    }
}

/**
 * @brief 控制器错误回调
 */
static void on_error(MediaController *controller,
                     OperationType operation,
                     MediaErrorCode code,
                     const char *message,
                     void *user_data) {
    (void)controller;
    (void)operation;
    (void)user_data;
    
    fprintf(stderr, "Error [%d]: %s\n", code, message);
}

/**
 * @brief 打印使用帮助
 */
static void print_usage(const char *program) {
    printf("GStreamer Media Framework v1.0.0\n");
    printf("\nUsage: %s <command> [options]\n\n", program);
    
    printf("Commands:\n");
    printf("  play <uri>              Play media file\n");
    printf("  record [options]        Record from camera/microphone\n");
    printf("  transcode [options]     Transcode media file\n");
    printf("  devices                 List available devices\n");
    printf("  info <file>             Show media file information\n\n");
    
    printf("Play options:\n");
    printf("  --low-latency           Enable low latency mode\n");
    printf("  --video-sink <sink>     Video output sink (xvimagesink, glimagesink)\n");
    printf("  --audio-sink <sink>     Audio output sink (autoaudiosink, alsasink)\n\n");
    
    printf("Record options:\n");
    printf("  -o, --output <file>     Output file path\n");
    printf("  --video-device <dev>    Video device (e.g., /dev/video0)\n");
    printf("  --audio-device <dev>    Audio device (e.g., hw:0,0)\n");
    printf("  --video-codec <codec>   Video codec (h264, h265)\n");
    printf("  --audio-codec <codec>   Audio codec (aac, opus)\n");
    printf("  --width <n>             Video width\n");
    printf("  --height <n>            Video height\n");
    printf("  --framerate <n>         Video framerate\n");
    printf("  --duration <sec>        Recording duration in seconds\n\n");
    
    printf("Transcode options:\n");
    printf("  -i, --input <file>      Input file path\n");
    printf("  -o, --output <file>     Output file path\n");
    printf("  --video-codec <codec>   Video codec (h264, h265, copy)\n");
    printf("  --audio-codec <codec>   Audio codec (aac, opus, copy)\n");
    printf("  --bitrate <bps>         Video bitrate\n");
    printf("  --start <sec>           Start time in seconds\n");
    printf("  --end <sec>             End time in seconds\n\n");
    
    printf("General options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --verbose           Verbose output\n");
    printf("  --log-file <file>       Log file path\n\n");
    
    printf("Examples:\n");
    printf("  %s play /path/to/video.mp4\n", program);
    printf("  %s record -o output.mp4 --video-device /dev/video0 --duration 10\n", program);
    printf("  %s transcode -i input.mp4 -o output.mp4 --video-codec h265 --audio-codec opus\n", program);
}

/**
 * @brief 列出可用设备
 */
static int list_devices(void) {
    char devices[16][256];
    int count;
    
    printf("Available video devices:\n");
    count = controller_enum_video_devices(devices, 16);
    if (count == 0) {
        printf("  No video devices found\n");
    } else {
        for (int i = 0; i < count; i++) {
            printf("  [%d] %s\n", i, devices[i]);
        }
    }
    
    printf("\nAvailable audio devices:\n");
    count = controller_enum_audio_devices(devices, 16);
    if (count == 0) {
        printf("  No audio devices found\n");
    } else {
        for (int i = 0; i < count; i++) {
            printf("  [%d] %s\n", i, devices[i]);
        }
    }
    
    return 0;
}

/**
 * @brief 显示媒体文件信息
 */
static int show_info(const char *file_path) {
    if (file_path == NULL) {
        fprintf(stderr, "Error: No file specified\n");
        return -1;
    }
    
    /* 创建播放器获取信息 */
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    if (player == NULL) {
        fprintf(stderr, "Error: Failed to create player\n");
        return -1;
    }
    
    MediaErrorCode ret = player_set_uri(player, file_path);
    if (ret != MEDIA_OK) {
        fprintf(stderr, "Error: Failed to set URI\n");
        player_destroy(player);
        return -1;
    }
    
    MediaInfo info;
    ret = player_get_media_info(player, &info);
    if (ret != MEDIA_OK) {
        fprintf(stderr, "Error: Failed to get media info\n");
        player_destroy(player);
        return -1;
    }
    
    printf("Media Information:\n");
    printf("  File: %s\n", file_path);
    printf("  Duration: %.2f seconds\n", info.duration / 1000000000.0);
    printf("  Seekable: %s\n", info.seekable ? "Yes" : "No");
    
    if (info.has_video) {
        printf("\nVideo Stream:\n");
        printf("  Codec: %s\n", video_codec_to_string(info.video_params.codec));
        printf("  Resolution: %dx%d\n", info.video_params.width, info.video_params.height);
        printf("  Framerate: %d/%d fps\n", info.video_params.framerate_num, 
               info.video_params.framerate_den);
        printf("  Bitrate: %d bps\n", info.video_params.bitrate);
    }
    
    if (info.has_audio) {
        printf("\nAudio Stream:\n");
        printf("  Codec: %s\n", audio_codec_to_string(info.audio_params.codec));
        printf("  Sample Rate: %d Hz\n", info.audio_params.sample_rate);
        printf("  Channels: %d\n", info.audio_params.channels);
        printf("  Bitrate: %d bps\n", info.audio_params.bitrate);
    }
    
    player_destroy(player);
    return 0;
}

/**
 * @brief 播放媒体文件
 */
static int play_media(MediaController *controller, const char *uri, int low_latency) {
    if (controller == NULL || uri == NULL) {
        return -1;
    }

    /* 控制器当前默认启用低延迟；保留参数供后续运行时配置 API 使用。 */
    (void)low_latency;
    
    MediaErrorCode ret = controller_play(controller, uri);
    if (ret != MEDIA_OK) {
        fprintf(stderr, "Error: Failed to play media (%d)\n", ret);
        return -1;
    }
    
    printf("Playing: %s\n", uri);
    printf("Press Ctrl+C to stop...\n");
    
    /* 等待播放结束 */
    while (g_running && controller_get_state(controller) != MEDIA_STATE_EOS) {
        process_main_context();
        if (controller_get_state(controller) == MEDIA_STATE_ERROR) {
            break;
        }
        usleep(100000);  /* 100ms */
    }
    
    controller_stop(controller);
    return 0;
}

/**
 * @brief 录制媒体
 */
static int record_media(MediaController *controller, const char *output_file,
                        const char *video_device, const char *audio_device,
                        VideoCodec video_codec, AudioCodec audio_codec,
                        int width, int height, int framerate, int duration) {
    if (controller == NULL || output_file == NULL) {
        return -1;
    }
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    /* 设置输出文件 */
    strncpy(config.output_file, output_file, sizeof(config.output_file) - 1);
    
    /* 设置视频源 */
    if (video_device != NULL) {
        config.video_source = VIDEO_SOURCE_V4L2;
        strncpy(config.video_device, video_device, sizeof(config.video_device) - 1);
    } else {
        config.video_source = VIDEO_SOURCE_TEST;  /* 使用测试模式 */
    }
    
    /* 设置音频源 */
    if (audio_device != NULL) {
        config.audio_source = AUDIO_SOURCE_ALSA;
        strncpy(config.audio_device, audio_device, sizeof(config.audio_device) - 1);
    } else {
        config.audio_source = AUDIO_SOURCE_TEST;  /* 使用测试模式 */
    }
    
    /* 设置视频参数 */
    config.video_params.codec = video_codec;
    config.video_params.width = width;
    config.video_params.height = height;
    config.video_params.framerate_num = framerate;
    config.video_params.framerate_den = 1;
    config.video_params.bitrate = 4000000;  /* 4 Mbps */
    
    /* 设置音频参数 */
    config.audio_params.codec = audio_codec;
    config.audio_params.sample_rate = 44100;
    config.audio_params.channels = 2;
    config.audio_params.bitrate = 128000;  /* 128 kbps */
    
    /* 设置容器格式 */
    config.container = CONTAINER_MP4;
    
    /* 设置低延迟模式 */
    config.low_latency = 1;
    
    printf("Recording to: %s\n", output_file);
    printf("Video: %s %dx%d @ %d fps\n", video_codec_to_string(video_codec),
           width, height, framerate);
    printf("Audio: %s %d Hz %d ch\n", audio_codec_to_string(audio_codec),
           config.audio_params.sample_rate, config.audio_params.channels);
    
    MediaErrorCode ret = controller_start_recording(controller, &config);
    if (ret != MEDIA_OK) {
        fprintf(stderr, "Error: Failed to start recording (%d)\n", ret);
        return -1;
    }
    
    printf("Recording started. Press Ctrl+C to stop...\n");
    
    /* 等待录制结束 */
    int elapsed = 0;
    while (g_running && (duration == 0 || elapsed < duration)) {
        process_main_context();
        if (controller_get_state(controller) == MEDIA_STATE_ERROR) {
            break;
        }
        sleep(1);
        elapsed++;
        
        if (duration > 0) {
            printf("\rRecording: %d/%d seconds", elapsed, duration);
            fflush(stdout);
        }
    }
    
    printf("\n");
    controller_stop(controller);
    printf("Recording saved to: %s\n", output_file);
    
    return 0;
}

/**
 * @brief 转码媒体文件
 */
static int transcode_media(MediaController *controller,
                           const char *input_file, const char *output_file,
                           VideoCodec video_codec, AudioCodec audio_codec,
                           int bitrate, int start_time, int end_time) {
    if (controller == NULL || input_file == NULL || output_file == NULL) {
        return -1;
    }
    
    TranscoderConfig config;
    transcoder_config_init(&config);
    
    /* 设置输入输出文件 */
    strncpy(config.input_file, input_file, sizeof(config.input_file) - 1);
    strncpy(config.output_file, output_file, sizeof(config.output_file) - 1);
    
    /* 设置容器格式 */
    config.output_container = CONTAINER_MP4;
    
    /* 设置视频转码 */
    if (video_codec != VIDEO_CODEC_NONE) {
        config.video_transcode = 1;
        config.video_params.codec = video_codec;
        config.video_params.bitrate = bitrate > 0 ? bitrate : 4000000;
    } else {
        config.video_transcode = 0;  /* 复制视频流 */
    }
    
    /* 设置音频转码 */
    if (audio_codec != AUDIO_CODEC_NONE) {
        config.audio_transcode = 1;
        config.audio_params.codec = audio_codec;
        config.audio_params.bitrate = 128000;
    } else {
        config.audio_transcode = 0;  /* 复制音频流 */
    }
    
    /* 设置转码范围 */
    config.start_time = (int64_t)start_time * 1000000000LL;
    config.end_time = (int64_t)end_time * 1000000000LL;
    
    /* 设置低延迟模式 */
    config.low_latency = 1;
    
    printf("Transcoding: %s -> %s\n", input_file, output_file);
    if (config.video_transcode) {
        printf("Video: %s @ %d bps\n", video_codec_to_string(video_codec), 
               config.video_params.bitrate);
    } else {
        printf("Video: copy\n");
    }
    if (config.audio_transcode) {
        printf("Audio: %s\n", audio_codec_to_string(audio_codec));
    } else {
        printf("Audio: copy\n");
    }
    
    MediaErrorCode ret = controller_start_transcoding(controller, &config);
    if (ret != MEDIA_OK) {
        fprintf(stderr, "Error: Failed to start transcoding (%d)\n", ret);
        return -1;
    }
    
    printf("Transcoding started...\n");
    
    /* 等待转码结束 */
    TranscodeProgress progress;
    while (g_running) {
        process_main_context();
        MediaState state = controller_get_state(controller);
        if (state == MEDIA_STATE_EOS || state == MEDIA_STATE_READY ||
            state == MEDIA_STATE_ERROR) {
            break;
        }
        
        if (transcoder_get_progress(controller_get_transcoder(controller), &progress) == MEDIA_OK) {
            printf("\rProgress: %.1f%% (%.1f/%.1f sec)", 
                   progress.progress,
                   progress.position / 1000000000.0,
                   progress.duration / 1000000000.0);
            fflush(stdout);
        }
        
        usleep(200000);  /* 200ms */
    }
    
    printf("\n");
    controller_stop(controller);
    printf("Transcoding completed: %s\n", output_file);
    
    return 0;
}

/**
 * @brief 主函数
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }
    
    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* 解析命令 */
    const char *command = argv[1];
    
    /* 初始化控制器配置 */
    ControllerConfig config;
    controller_config_init(&config);
    config.log_level = LOG_LEVEL_INFO;
    config.log_to_console = 1;
    config.low_latency = 1;
    
    /* 创建控制器 */
    g_controller = controller_create(&config);
    if (g_controller == NULL) {
        fprintf(stderr, "Error: Failed to create media controller\n");
        return -1;
    }
    
    /* 设置回调 */
    ControllerCallbacks callbacks;
    controller_callbacks_init(&callbacks);
    callbacks.on_state_changed = on_state_changed;
    callbacks.on_error = on_error;
    controller_set_callbacks(g_controller, &callbacks);
    
    int ret = 0;
    
    /* 处理命令 */
    if (strcmp(command, "play") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: No URI specified\n");
            print_usage(argv[0]);
            ret = -1;
        } else {
            int low_latency = 0;
            for (int i = 3; i < argc; i++) {
                if (strcmp(argv[i], "--low-latency") == 0) {
                    low_latency = 1;
                }
            }
            ret = play_media(g_controller, argv[2], low_latency);
        }
    }
    else if (strcmp(command, "record") == 0) {
        const char *output_file = "output.mp4";
        const char *video_device = NULL;
        const char *audio_device = NULL;
        VideoCodec video_codec = VIDEO_CODEC_H264;
        AudioCodec audio_codec = AUDIO_CODEC_AAC;
        int width = 1920;
        int height = 1080;
        int framerate = 30;
        int duration = 0;
        
        static struct option long_options[] = {
            {"output", required_argument, 0, 'o'},
            {"video-device", required_argument, 0, 'V'},
            {"audio-device", required_argument, 0, 'A'},
            {"video-codec", required_argument, 0, 'v'},
            {"audio-codec", required_argument, 0, 'a'},
            {"width", required_argument, 0, 'W'},
            {"height", required_argument, 0, 'H'},
            {"framerate", required_argument, 0, 'f'},
            {"duration", required_argument, 0, 'd'},
            {0, 0, 0, 0}
        };
        
        int opt;
        int option_index = 0;
        
        /* 跳过 "record" 参数 */
        optind = 2;
        
        while ((opt = getopt_long(argc, argv, "o:", long_options, &option_index)) != -1) {
            switch (opt) {
                case 'o':
                    output_file = optarg;
                    break;
                case 'V':
                    video_device = optarg;
                    break;
                case 'A':
                    audio_device = optarg;
                    break;
                case 'v':
                    video_codec = video_codec_from_string(optarg);
                    break;
                case 'a':
                    audio_codec = audio_codec_from_string(optarg);
                    break;
                case 'W':
                    width = atoi(optarg);
                    break;
                case 'H':
                    height = atoi(optarg);
                    break;
                case 'f':
                    framerate = atoi(optarg);
                    break;
                case 'd':
                    duration = atoi(optarg);
                    break;
                default:
                    break;
            }
        }
        
        ret = record_media(g_controller, output_file, video_device, audio_device,
                          video_codec, audio_codec, width, height, framerate, duration);
    }
    else if (strcmp(command, "transcode") == 0) {
        const char *input_file = NULL;
        const char *output_file = NULL;
        VideoCodec video_codec = VIDEO_CODEC_NONE;  /* 默认复制 */
        AudioCodec audio_codec = AUDIO_CODEC_NONE;  /* 默认复制 */
        int bitrate = 0;
        int start_time = 0;
        int end_time = 0;
        
        static struct option long_options[] = {
            {"input", required_argument, 0, 'i'},
            {"output", required_argument, 0, 'o'},
            {"video-codec", required_argument, 0, 'v'},
            {"audio-codec", required_argument, 0, 'a'},
            {"bitrate", required_argument, 0, 'b'},
            {"start", required_argument, 0, 's'},
            {"end", required_argument, 0, 'e'},
            {0, 0, 0, 0}
        };
        
        int opt;
        int option_index = 0;
        
        /* 跳过 "transcode" 参数 */
        optind = 2;
        
        while ((opt = getopt_long(argc, argv, "i:o:", long_options, &option_index)) != -1) {
            switch (opt) {
                case 'i':
                    input_file = optarg;
                    break;
                case 'o':
                    output_file = optarg;
                    break;
                case 'v':
                    if (strcmp(optarg, "copy") != 0) {
                        video_codec = video_codec_from_string(optarg);
                    }
                    break;
                case 'a':
                    if (strcmp(optarg, "copy") != 0) {
                        audio_codec = audio_codec_from_string(optarg);
                    }
                    break;
                case 'b':
                    bitrate = atoi(optarg);
                    break;
                case 's':
                    start_time = atoi(optarg);
                    break;
                case 'e':
                    end_time = atoi(optarg);
                    break;
                default:
                    break;
            }
        }
        
        if (input_file == NULL || output_file == NULL) {
            fprintf(stderr, "Error: Input and output files must be specified\n");
            print_usage(argv[0]);
            ret = -1;
        } else {
            ret = transcode_media(g_controller, input_file, output_file,
                                 video_codec, audio_codec, bitrate, start_time, end_time);
        }
    }
    else if (strcmp(command, "devices") == 0) {
        ret = list_devices();
    }
    else if (strcmp(command, "info") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: No file specified\n");
            print_usage(argv[0]);
            ret = -1;
        } else {
            ret = show_info(argv[2]);
        }
    }
    else if (strcmp(command, "help") == 0 || strcmp(command, "-h") == 0 || 
             strcmp(command, "--help") == 0) {
        print_usage(argv[0]);
    }
    else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        ret = -1;
    }
    
    /* 清理 */
    controller_destroy(g_controller);
    
    return ret;
}
