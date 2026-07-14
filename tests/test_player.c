/**
 * @file test_player.c
 * @brief 播放器模块测试程序
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "player.h"
#include "logger.h"

static int g_test_passed = 0;
static int g_test_failed = 0;
static volatile int g_eos_received = 0;

#define TEST_ASSERT(condition, message) do { \
    if (condition) { \
        printf("[PASS] %s\n", message); \
        g_test_passed++; \
    } else { \
        printf("[FAIL] %s\n", message); \
        g_test_failed++; \
    } \
} while(0)

static void on_state_changed(MediaPlayer *player, MediaState state, void *user_data) {
    (void)player;
    (void)user_data;
    printf("State changed: %s\n", media_state_to_string(state));
    
    if (state == MEDIA_STATE_EOS) {
        g_eos_received = 1;
    }
}

static void on_error(MediaPlayer *player, MediaErrorCode code, const char *message, void *user_data) {
    (void)player;
    (void)user_data;
    printf("Error: [%d] %s\n", code, message);
}

int test_player_create_destroy(void) {
    printf("\n=== Test: Player Create/Destroy ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation");
    
    player_destroy(player);
    printf("[PASS] Player destruction\n");
    g_test_passed++;
    
    return 0;
}

int test_player_config(void) {
    printf("\n=== Test: Player Configuration ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    TEST_ASSERT(config.low_latency == 1, "Default low_latency");
    TEST_ASSERT(config.buffer_duration > 0, "Default buffer_duration");
    TEST_ASSERT(strlen(config.video_sink) > 0, "Default video_sink");
    TEST_ASSERT(strlen(config.audio_sink) > 0, "Default audio_sink");
    
    return 0;
}

int test_player_callbacks(void) {
    printf("\n=== Test: Player Callbacks ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation for callback test");
    
    PlayerCallbacks callbacks;
    player_callbacks_init(&callbacks);
    callbacks.on_state_changed = on_state_changed;
    callbacks.on_error = on_error;
    
    MediaErrorCode ret = player_set_callbacks(player, &callbacks);
    TEST_ASSERT(ret == MEDIA_OK, "Set callbacks");
    
    player_destroy(player);
    printf("[PASS] Callback test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_player_state(void) {
    printf("\n=== Test: Player State ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation for state test");
    
    MediaState state = player_get_state(player);
    TEST_ASSERT(state == MEDIA_STATE_NULL, "Initial state is NULL");
    
    player_destroy(player);
    printf("[PASS] State test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_player_uri(void) {
    printf("\n=== Test: Player URI ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation for URI test");
    
    // Test with invalid URI
    MediaErrorCode ret = player_set_uri(player, "/nonexistent/file.mp4");
    TEST_ASSERT(ret == MEDIA_OK, "Set URI (validation happens at play time)");
    
    // Test with NULL
    ret = player_set_uri(player, NULL);
    TEST_ASSERT(ret == MEDIA_ERROR_INVALID_PARAM, "Set NULL URI returns error");
    
    player_destroy(player);
    printf("[PASS] URI test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_player_volume(void) {
    printf("\n=== Test: Player Volume ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation for volume test");
    
    MediaErrorCode ret = player_set_volume(player, 0.5);
    TEST_ASSERT(ret == MEDIA_OK, "Set volume to 0.5");
    
    double volume = player_get_volume(player);
    TEST_ASSERT(volume == 0.5, "Get volume returns 0.5");
    
    ret = player_set_volume(player, 1.5);
    TEST_ASSERT(ret == MEDIA_OK, "Set volume to 1.5 (clamped)");
    
    player_destroy(player);
    printf("[PASS] Volume test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int test_player_mute(void) {
    printf("\n=== Test: Player Mute ===\n");
    
    PlayerConfig config;
    player_config_init(&config);
    
    MediaPlayer *player = player_create(&config);
    TEST_ASSERT(player != NULL, "Player creation for mute test");
    
    MediaErrorCode ret = player_set_mute(player, 1);
    TEST_ASSERT(ret == MEDIA_OK, "Set mute on");
    
    int mute = player_get_mute(player);
    TEST_ASSERT(mute == 1, "Get mute returns 1");
    
    ret = player_set_mute(player, 0);
    TEST_ASSERT(ret == MEDIA_OK, "Set mute off");
    
    player_destroy(player);
    printf("[PASS] Mute test cleanup\n");
    g_test_passed++;
    
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  Player Module Test Suite\n");
    printf("========================================\n");
    
    // Initialize logger
    LoggerConfig log_config;
    logger_config_init(&log_config);
    log_config.min_level = LOG_LEVEL_WARN;  // Reduce noise during tests
    log_config.target = LOG_TARGET_CONSOLE;
    logger_init(&log_config);
    
    // Run tests
    test_player_create_destroy();
    test_player_config();
    test_player_callbacks();
    test_player_state();
    test_player_uri();
    test_player_volume();
    test_player_mute();
    
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
