/**
 * @file test_recorder.c
 * @brief 录制器模块测试程序
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "recorder.h"
#include "logger.h"

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    if (condition) { \
        printf("[PASS] %s\n", message); \
        g_test_passed++; \
    } else { \
        printf("[FAIL] %s\n", message); \
        g_test_failed++; \
    } \
} while(0)

int test_recorder_create_destroy(void) {
    printf("\n=== Test: Recorder Create/Destroy ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    MediaRecorder *recorder = recorder_create(&config);
    TEST_ASSERT(recorder != NULL, "Recorder creation");
    
    recorder_destroy(recorder);
    printf("[PASS] Recorder destruction\n");
    g_test_passed++;
    
    return 0;
}

int test_recorder_config(void) {
    printf("\n=== Test: Recorder Configuration ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    TEST_ASSERT(config.video_source == VIDEO_SOURCE_V4L2, "Default video_source");
    TEST_ASSERT(config.audio_source == AUDIO_SOURCE_ALSA, "Default audio_source");
    TEST_ASSERT(config.container == CONTAINER_MP4, "Default container is MP4");
    TEST_ASSERT(config.low_latency == 1, "Default low_latency");
    
    return 0;
}

int test_recorder_video_params(void) {
    printf("\n=== Test: Recorder Video Parameters ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    MediaRecorder *recorder = recorder_create(&config);
    TEST_ASSERT(recorder != NULL, "Recorder creation for video params test");
    
    VideoParams params;
    video_params_init(&params);
    params.codec = VIDEO_CODEC_H265;
    params.width = 3840;
    params.height = 2160;
    params.bitrate = 8000000;
    
    MediaErrorCode ret = recorder_set_video_params(recorder, &params);
    TEST_ASSERT(ret == MEDIA_OK, "Set video params");
    
    recorder_destroy(recorder);
    printf("[PASS] Video params test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_recorder_audio_params(void) {
    printf("\n=== Test: Recorder Audio Parameters ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    MediaRecorder *recorder = recorder_create(&config);
    TEST_ASSERT(recorder != NULL, "Recorder creation for audio params test");
    
    AudioParams params;
    audio_params_init(&params);
    params.codec = AUDIO_CODEC_OPUS;
    params.sample_rate = 48000;
    params.channels = 2;
    params.bitrate = 128000;
    
    MediaErrorCode ret = recorder_set_audio_params(recorder, &params);
    TEST_ASSERT(ret == MEDIA_OK, "Set audio params");
    
    recorder_destroy(recorder);
    printf("[PASS] Audio params test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_recorder_output_file(void) {
    printf("\n=== Test: Recorder Output File ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    MediaRecorder *recorder = recorder_create(&config);
    TEST_ASSERT(recorder != NULL, "Recorder creation for output file test");
    
    MediaErrorCode ret = recorder_set_output_file(recorder, "/tmp/test_output.mp4");
    TEST_ASSERT(ret == MEDIA_OK, "Set output file");
    
    // Test with NULL
    ret = recorder_set_output_file(recorder, NULL);
    TEST_ASSERT(ret == MEDIA_ERROR_INVALID_PARAM, "Set NULL output file returns error");
    
    recorder_destroy(recorder);
    printf("[PASS] Output file test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_recorder_state(void) {
    printf("\n=== Test: Recorder State ===\n");
    
    RecorderConfig config;
    recorder_config_init(&config);
    
    MediaRecorder *recorder = recorder_create(&config);
    TEST_ASSERT(recorder != NULL, "Recorder creation for state test");
    
    MediaState state = recorder_get_state(recorder);
    TEST_ASSERT(state == MEDIA_STATE_NULL, "Initial state is NULL");
    
    recorder_destroy(recorder);
    printf("[PASS] State test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_enum_devices(void) {
    printf("\n=== Test: Enumerate Devices ===\n");
    
    char video_devices[16][256];
    char audio_devices[16][256];
    
    int video_count = recorder_enum_video_devices(video_devices, 16);
    printf("Found %d video device(s)\n", video_count);
    for (int i = 0; i < video_count; i++) {
        printf("  Video device %d: %s\n", i, video_devices[i]);
    }
    TEST_ASSERT(video_count >= 0, "Enumerate video devices");
    
    int audio_count = recorder_enum_audio_devices(audio_devices, 16);
    printf("Found %d audio device(s)\n", audio_count);
    for (int i = 0; i < audio_count; i++) {
        printf("  Audio device %d: %s\n", i, audio_devices[i]);
    }
    TEST_ASSERT(audio_count >= 0, "Enumerate audio devices");
    
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  Recorder Module Test Suite\n");
    printf("========================================\n");
    
    // Initialize logger
    LoggerConfig log_config;
    logger_config_init(&log_config);
    log_config.min_level = LOG_LEVEL_WARN;
    log_config.target = LOG_TARGET_CONSOLE;
    logger_init(&log_config);
    
    // Run tests
    test_recorder_create_destroy();
    test_recorder_config();
    test_recorder_video_params();
    test_recorder_audio_params();
    test_recorder_output_file();
    test_recorder_state();
    test_enum_devices();
    
    // Print summary
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_test_passed);
    printf("Failed: %d\n", g_test_failed);
    printf("Total:  %d\n", g_test_passed + g_test_failed);
    printf("========================================\n");
    
    logger_shutdown();
    
    return g_test_failed > 0 ? 1 : 0;
}
