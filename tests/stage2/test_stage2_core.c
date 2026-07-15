#include "learning_stage.h"
#include "player.h"
#include "recorder.h"
#include "transcoder.h"

#include <gst/gst.h>
#include <stdio.h>
#include <sys/stat.h>

#if !MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M2)
#error "Stage 2 tests require MEDIA_FRAMEWORK_STAGE >= 2"
#endif

#define INPUT_FILE "/tmp/media_framework_stage2_input.mp4"
#define RECORD_FILE "/tmp/media_framework_stage2_record.mp4"
#define TRANSCODE_FILE "/tmp/media_framework_stage2_transcode.mp4"
#define COPY_FILE "/tmp/media_framework_stage2_copy.mp4"
#define MIXED_FILE "/tmp/media_framework_stage2_mixed.mp4"

static int failures = 0;

typedef struct {
    gint progress_calls;
    gint eos_received;
} RecorderTestContext;

#define CHECK(condition, message) do { \
    if (condition) { \
        printf("[PASS] %s\n", message); \
    } else { \
        fprintf(stderr, "[FAIL] %s\n", message); \
        ++failures; \
    } \
} while (0)

static gboolean file_has_data(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

static gboolean run_description(const char *description) {
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(description, &error);
    if (pipeline == NULL || error != NULL) {
        fprintf(stderr, "Pipeline construction failed: %s\n",
                error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        if (pipeline != NULL) gst_object_unref(pipeline);
        return FALSE;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    gboolean ok = gst_element_set_state(pipeline, GST_STATE_PLAYING) !=
                  GST_STATE_CHANGE_FAILURE;
    GstMessage *message = ok
        ? gst_bus_timed_pop_filtered(
              bus, 20 * GST_SECOND,
              GST_MESSAGE_EOS | GST_MESSAGE_ERROR)
        : NULL;
    if (message == NULL || GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        if (message != NULL) {
            GError *pipeline_error = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &pipeline_error, &debug);
            fprintf(stderr, "Pipeline failed: %s (%s)\n",
                    pipeline_error->message,
                    debug != NULL ? debug : "no details");
            g_clear_error(&pipeline_error);
            g_free(debug);
        }
        ok = FALSE;
    }

    if (message != NULL) gst_message_unref(message);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    return ok;
}

static gboolean wait_for_player(MediaPlayer *player, gint64 timeout_us) {
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    while (g_get_monotonic_time() < deadline) {
        MediaState state = player_get_state(player);
        if (state == MEDIA_STATE_EOS) return TRUE;
        if (state == MEDIA_STATE_ERROR) return FALSE;
        g_usleep(10000);
    }
    return FALSE;
}

static void on_player_position(MediaPlayer *player,
                               int64_t position,
                               void *user_data) {
    (void)player;
    (void)position;
    gint *position_calls = (gint *)user_data;
    g_atomic_int_inc(position_calls);
}

static gboolean wait_for_transcoder(MediaTranscoder *transcoder,
                                    gint64 timeout_us) {
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    while (g_get_monotonic_time() < deadline) {
        MediaState state = transcoder_get_state(transcoder);
        if (state == MEDIA_STATE_EOS) return TRUE;
        if (state == MEDIA_STATE_ERROR) return FALSE;
        TranscodeProgress progress;
        transcoder_get_progress(transcoder, &progress);
        g_usleep(20000);
    }
    return FALSE;
}

static void on_recorder_state(MediaRecorder *recorder,
                              MediaState state,
                              void *user_data) {
    (void)recorder;
    RecorderTestContext *context = (RecorderTestContext *)user_data;
    if (state == MEDIA_STATE_EOS) {
        g_atomic_int_set(&context->eos_received, TRUE);
    }
}

static void on_recorder_progress(MediaRecorder *recorder,
                                 int64_t duration,
                                 int64_t size,
                                 void *user_data) {
    (void)recorder;
    (void)duration;
    (void)size;
    RecorderTestContext *context = (RecorderTestContext *)user_data;
    g_atomic_int_inc(&context->progress_calls);
}

static void create_input_file(void) {
    remove(INPUT_FILE);
    const char *description =
        "mp4mux name=mux ! filesink location=" INPUT_FILE " "
        "videotestsrc num-buffers=45 ! "
        "video/x-raw,width=320,height=240,framerate=30/1 ! "
        "x264enc tune=zerolatency bitrate=512 key-int-max=30 ! "
        "h264parse ! queue ! mux. "
        "audiotestsrc num-buffers=70 ! "
        "audio/x-raw,rate=48000,channels=2 ! "
        "audioconvert ! avenc_aac bitrate=96000 ! aacparse ! queue ! mux.";
    CHECK(run_description(description), "Generate deterministic AV input");
    CHECK(file_has_data(INPUT_FILE), "Generated input file has data");
}

static void test_player(void) {
    PlayerConfig config;
    player_config_init(&config);
    g_strlcpy(config.video_sink, "fakesink", sizeof(config.video_sink));
    g_strlcpy(config.audio_sink, "fakesink", sizeof(config.audio_sink));
    config.sync = 1;
    config.position_update_interval = 20;

    MediaPlayer *player = player_create(&config);
    CHECK(player != NULL, "Create stage 2 player");
    if (player == NULL) return;

    CHECK(player_open(player, INPUT_FILE) == MEDIA_OK,
          "Open generated media with stage 2 player");
    MediaInfo info;
    CHECK(player_get_media_info(player, &info) == MEDIA_OK,
          "Read media information");
    CHECK(info.has_video && info.has_audio,
          "Discover video and audio streams");
    CHECK(info.duration > 0, "Discover media duration");
    gint position_calls = 0;
    PlayerCallbacks callbacks;
    player_callbacks_init(&callbacks);
    callbacks.on_position = on_player_position;
    callbacks.user_data = &position_calls;
    player_set_callbacks(player, &callbacks);
    CHECK(player_play(player) == MEDIA_OK, "Start stage 2 playback");
    CHECK(wait_for_player(player, 10 * G_TIME_SPAN_SECOND),
          "Receive playback EOS without external GLib loop");
    CHECK(g_atomic_int_get(&position_calls) > 0,
          "Player emits periodic position callbacks");
    player_destroy(player);
}

static void test_recorder(void) {
    remove(RECORD_FILE);
    RecorderConfig config;
    recorder_config_init(&config);
    config.video_source = VIDEO_SOURCE_TEST;
    config.audio_source = AUDIO_SOURCE_TEST;
    config.enable_hardware_encode = 0;
    config.video_params.width = 320;
    config.video_params.height = 240;
    config.video_params.framerate_num = 30;
    config.video_params.framerate_den = 1;
    config.video_params.codec = VIDEO_CODEC_H264;
    config.video_params.bitrate = 512000;
    config.audio_params.sample_rate = 48000;
    config.audio_params.channels = 2;
    config.audio_params.codec = AUDIO_CODEC_AAC;
    config.audio_params.bitrate = 96000;
    config.max_duration = 300 * GST_MSECOND;
    g_strlcpy(config.output_file, RECORD_FILE, sizeof(config.output_file));

    MediaRecorder *recorder = recorder_create(&config);
    CHECK(recorder != NULL, "Create stage 2 recorder");
    if (recorder == NULL) return;

    RecorderTestContext context = {0};
    RecorderCallbacks callbacks;
    recorder_callbacks_init(&callbacks);
    callbacks.on_state_changed = on_recorder_state;
    callbacks.on_progress = on_recorder_progress;
    callbacks.user_data = &context;
    recorder_set_callbacks(recorder, &callbacks);

    CHECK(recorder_start(recorder) == MEDIA_OK,
          "Start test-source recording");
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!g_atomic_int_get(&context.eos_received) &&
           g_get_monotonic_time() < deadline) {
        g_usleep(10000);
    }
    CHECK(g_atomic_int_get(&context.eos_received),
          "Recorder enforces maximum duration");
    CHECK(recorder_stop(recorder) == MEDIA_OK,
          "Stop recorder with EOS finalization");
    CHECK(g_atomic_int_get(&context.progress_calls) > 0,
          "Recorder emits progress callbacks");
    CHECK(recorder_get_duration(recorder) > 0,
          "Recorder reports elapsed duration");
    CHECK(recorder_get_file_size(recorder) > 0,
          "Recorder creates a finalized MP4");
    recorder_destroy(recorder);
}

static void run_transcoder_case(const char *output,
                                gboolean video_transcode,
                                gboolean audio_transcode,
                                const char *label) {
    remove(output);
    TranscoderConfig config;
    transcoder_config_init(&config);
    g_strlcpy(config.input_file, INPUT_FILE, sizeof(config.input_file));
    g_strlcpy(config.output_file, output, sizeof(config.output_file));
    config.output_container = CONTAINER_MP4;
    config.video_transcode = video_transcode;
    config.audio_transcode = audio_transcode;
    config.video_params.codec = VIDEO_CODEC_H264;
    config.video_params.bitrate = 384000;
    config.audio_params.codec = AUDIO_CODEC_AAC;
    config.audio_params.bitrate = 64000;
    if (video_transcode && audio_transcode) {
        config.start_time = 200 * GST_MSECOND;
        config.end_time = 900 * GST_MSECOND;
    }

    MediaTranscoder *transcoder = transcoder_create(&config);
    CHECK(transcoder != NULL, label);
    if (transcoder == NULL) return;

    CHECK(transcoder_start(transcoder) == MEDIA_OK, label);
    CHECK(wait_for_transcoder(transcoder, 20 * G_TIME_SPAN_SECOND), label);
    TranscodeProgress progress;
    CHECK(transcoder_get_progress(transcoder, &progress) == MEDIA_OK,
          "Read transcode progress");
    CHECK(progress.progress >= 99.0, "Completed progress reaches 100%");
    CHECK(transcoder_stop(transcoder) == MEDIA_OK,
          "Release transcode pipeline");
    CHECK(file_has_data(output), label);
    transcoder_destroy(transcoder);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    create_input_file();
    if (failures == 0) {
        test_player();
        test_recorder();
        run_transcoder_case(
            TRANSCODE_FILE, TRUE, TRUE, "Full transcode path");
        run_transcoder_case(
            COPY_FILE, FALSE, FALSE, "Full stream-copy path");
        run_transcoder_case(
            MIXED_FILE, TRUE, FALSE, "Mixed video-transcode/audio-copy path");
    }

    remove(INPUT_FILE);
    remove(RECORD_FILE);
    remove(TRANSCODE_FILE);
    remove(COPY_FILE);
    remove(MIXED_FILE);
    printf("Stage 2 failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
