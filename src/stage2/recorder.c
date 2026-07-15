#include "recorder.h"

#include "learning_stage.h"
#include "logger.h"

#include <dirent.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M2)
#error "The stage 2 recorder requires MEDIA_FRAMEWORK_STAGE >= 2"
#endif

struct MediaRecorder {
    RecorderConfig config;
    RecorderCallbacks callbacks;
    GstElement *pipeline;
    GstBus *bus;
    GMutex mutex;
    GCond condition;
    GThread *progress_thread;
    gint progress_thread_running;
    MediaState state;
    gboolean eos_received;
    gboolean error_received;
    gint64 start_time_us;
    int64_t duration;
    int64_t file_size;
};

static gpointer progress_thread_main(gpointer data) {
    MediaRecorder *recorder = (MediaRecorder *)data;
    while (g_atomic_int_get(&recorder->progress_thread_running)) {
        g_usleep(100000);
        if (!g_atomic_int_get(&recorder->progress_thread_running)) break;

        int64_t duration = recorder_get_duration(recorder);
        int64_t size = recorder_get_file_size(recorder);
        RecorderCallbacks callbacks;
        g_mutex_lock(&recorder->mutex);
        callbacks = recorder->callbacks;
        g_mutex_unlock(&recorder->mutex);
        if (callbacks.on_progress != NULL) {
            callbacks.on_progress(
                recorder, duration, size, callbacks.user_data);
        }

        gboolean duration_limit =
            recorder->config.max_duration > 0 &&
            duration >= recorder->config.max_duration;
        gboolean size_limit =
            recorder->config.max_size > 0 &&
            size >= recorder->config.max_size;
        if (duration_limit || size_limit) {
            g_atomic_int_set(&recorder->progress_thread_running, FALSE);
            gst_element_send_event(
                recorder->pipeline, gst_event_new_eos());
        }
    }
    return NULL;
}

static void recorder_notify_state(MediaRecorder *recorder, MediaState state) {
    RecorderCallbacks callbacks;
    g_mutex_lock(&recorder->mutex);
    recorder->state = state;
    callbacks = recorder->callbacks;
    g_mutex_unlock(&recorder->mutex);
    if (callbacks.on_state_changed != NULL) {
        callbacks.on_state_changed(recorder, state, callbacks.user_data);
    }
}

static GstBusSyncReply recorder_bus_sync(GstBus *bus,
                                         GstMessage *message,
                                         gpointer data) {
    (void)bus;
    MediaRecorder *recorder = (MediaRecorder *)data;

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        LOG_ERROR("Stage 2 recorder error: %s (%s)",
                  error->message, debug != NULL ? debug : "no details");

        RecorderCallbacks callbacks;
        g_mutex_lock(&recorder->mutex);
        recorder->error_received = TRUE;
        callbacks = recorder->callbacks;
        g_cond_broadcast(&recorder->condition);
        g_mutex_unlock(&recorder->mutex);
        if (callbacks.on_error != NULL) {
            callbacks.on_error(recorder, MEDIA_ERROR_GST_MESSAGE_ERROR,
                               error->message, callbacks.user_data);
        }
        g_clear_error(&error);
        g_free(debug);
        g_atomic_int_set(&recorder->progress_thread_running, FALSE);
        recorder_notify_state(recorder, MEDIA_STATE_ERROR);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        g_mutex_lock(&recorder->mutex);
        recorder->eos_received = TRUE;
        g_cond_broadcast(&recorder->condition);
        g_mutex_unlock(&recorder->mutex);
        g_atomic_int_set(&recorder->progress_thread_running, FALSE);
        recorder_notify_state(recorder, MEDIA_STATE_EOS);
    }

    return GST_BUS_DROP;
}

static GstElement *make_first_available(const char *const *names,
                                        const char *instance_name) {
    for (int i = 0; names[i] != NULL; ++i) {
        GstElement *element =
            gst_element_factory_make(names[i], instance_name);
        if (element != NULL) return element;
    }
    return NULL;
}

static GstElement *create_video_encoder(const RecorderConfig *config) {
    const char *h264_hw[] = {"nvh264enc", "vaapih264enc", NULL};
    const char *h265_hw[] = {"nvh265enc", "vaapih265enc", NULL};
    const char *h264_sw[] = {"x264enc", NULL};
    const char *h265_sw[] = {"x265enc", NULL};
    const char *vp8[] = {"vp8enc", NULL};
    const char *vp9[] = {"vp9enc", NULL};
    const char *const *names = NULL;

    if (config->video_params.codec == VIDEO_CODEC_H264) {
        names = config->enable_hardware_encode ? h264_hw : h264_sw;
    } else if (config->video_params.codec == VIDEO_CODEC_H265) {
        names = config->enable_hardware_encode ? h265_hw : h265_sw;
    } else if (config->video_params.codec == VIDEO_CODEC_VP8) {
        names = vp8;
    } else if (config->video_params.codec == VIDEO_CODEC_VP9) {
        names = vp9;
    }
    if (names == NULL) return NULL;

    GstElement *encoder = make_first_available(names, "video_encoder");
    if (encoder == NULL && config->enable_hardware_encode) {
        names = config->video_params.codec == VIDEO_CODEC_H264
            ? h264_sw : h265_sw;
        encoder = make_first_available(names, "video_encoder");
    }
    if (encoder != NULL) {
        if (g_object_class_find_property(
                G_OBJECT_GET_CLASS(encoder), "bitrate") != NULL) {
            g_object_set(encoder, "bitrate",
                         config->video_params.bitrate / 1000, NULL);
        }
        if (g_object_class_find_property(
                G_OBJECT_GET_CLASS(encoder), "key-int-max") != NULL) {
            g_object_set(encoder, "key-int-max",
                         config->video_params.gop_size, NULL);
        }
        if (config->low_latency &&
            g_object_class_find_property(
                G_OBJECT_GET_CLASS(encoder), "tune") != NULL) {
            g_object_set(encoder, "tune", 4, NULL);
        }
    }
    return encoder;
}

static GstElement *create_video_parser(VideoCodec codec) {
    if (codec == VIDEO_CODEC_H264) {
        return gst_element_factory_make("h264parse", "video_parser");
    }
    if (codec == VIDEO_CODEC_H265) {
        return gst_element_factory_make("h265parse", "video_parser");
    }
    return gst_element_factory_make("identity", "video_parser");
}

static GstElement *create_audio_encoder(const AudioParams *params) {
    GstElement *encoder = NULL;
    if (params->codec == AUDIO_CODEC_AAC) {
        const char *names[] = {"avenc_aac", "voaacenc", "faac", NULL};
        encoder = make_first_available(names, "audio_encoder");
    } else if (params->codec == AUDIO_CODEC_OPUS) {
        encoder = gst_element_factory_make("opusenc", "audio_encoder");
    } else if (params->codec == AUDIO_CODEC_VORBIS) {
        encoder = gst_element_factory_make("vorbisenc", "audio_encoder");
    } else if (params->codec == AUDIO_CODEC_FLAC) {
        encoder = gst_element_factory_make("flacenc", "audio_encoder");
    } else if (params->codec == AUDIO_CODEC_MP3) {
        encoder = gst_element_factory_make("lamemp3enc", "audio_encoder");
    }
    if (encoder != NULL &&
        g_object_class_find_property(
            G_OBJECT_GET_CLASS(encoder), "bitrate") != NULL) {
        g_object_set(encoder, "bitrate", params->bitrate, NULL);
    }
    return encoder;
}

static GstElement *create_audio_parser(AudioCodec codec) {
    if (codec == AUDIO_CODEC_AAC) {
        return gst_element_factory_make("aacparse", "audio_parser");
    }
    if (codec == AUDIO_CODEC_OPUS) {
        return gst_element_factory_make("opusparse", "audio_parser");
    }
    if (codec == AUDIO_CODEC_MP3) {
        return gst_element_factory_make("mpegaudioparse", "audio_parser");
    }
    return gst_element_factory_make("identity", "audio_parser");
}

static GstElement *create_muxer(ContainerFormat format) {
    const char *name = NULL;
    if (format == CONTAINER_MP4) name = "mp4mux";
    else if (format == CONTAINER_MKV) name = "matroskamux";
    else if (format == CONTAINER_WEBM) name = "webmmux";
    else if (format == CONTAINER_TS) name = "mpegtsmux";
    else if (format == CONTAINER_FLV) name = "flvmux";
    return name != NULL ? gst_element_factory_make(name, "muxer") : NULL;
}

static GstElement *create_video_source(const RecorderConfig *config) {
    GstElement *source = NULL;
    if (config->video_source == VIDEO_SOURCE_TEST) {
        source = gst_element_factory_make("videotestsrc", "video_source");
        if (source != NULL) g_object_set(source, "is-live", TRUE, NULL);
    } else if (config->video_source == VIDEO_SOURCE_V4L2) {
        source = gst_element_factory_make("v4l2src", "video_source");
        if (source != NULL) {
            g_object_set(source, "device", config->video_device, NULL);
        }
    }
    return source;
}

static GstElement *create_audio_source(const RecorderConfig *config) {
    GstElement *source = NULL;
    if (config->audio_source == AUDIO_SOURCE_TEST) {
        source = gst_element_factory_make("audiotestsrc", "audio_source");
        if (source != NULL) g_object_set(source, "is-live", TRUE, NULL);
    } else if (config->audio_source == AUDIO_SOURCE_ALSA) {
        source = gst_element_factory_make("alsasrc", "audio_source");
        if (source != NULL) {
            g_object_set(source, "device", config->audio_device, NULL);
        }
    } else if (config->audio_source == AUDIO_SOURCE_PULSE) {
        source = gst_element_factory_make("pulsesrc", "audio_source");
        if (source != NULL && config->audio_device[0] != '\0') {
            g_object_set(source, "device", config->audio_device, NULL);
        }
    }
    return source;
}

static void destroy_pipeline(MediaRecorder *recorder) {
    if (recorder->pipeline == NULL) return;
    gst_bus_set_sync_handler(recorder->bus, NULL, NULL, NULL);
    gst_element_set_state(recorder->pipeline, GST_STATE_NULL);
    gst_object_unref(recorder->bus);
    gst_object_unref(recorder->pipeline);
    recorder->bus = NULL;
    recorder->pipeline = NULL;
}

static MediaErrorCode build_pipeline(MediaRecorder *recorder) {
    GstElement *pipeline = gst_pipeline_new("stage2_recorder");
    GstElement *muxer = create_muxer(recorder->config.container);
    GstElement *sink = gst_element_factory_make("filesink", "output");
    if (pipeline == NULL || muxer == NULL || sink == NULL) {
        if (pipeline != NULL) gst_object_unref(pipeline);
        if (muxer != NULL) gst_object_unref(muxer);
        if (sink != NULL) gst_object_unref(sink);
        return MEDIA_ERROR_GST_ELEMENT_CREATE;
    }
    g_object_set(sink, "location", recorder->config.output_file, NULL);
    gst_bin_add_many(GST_BIN(pipeline), muxer, sink, NULL);
    if (!gst_element_link(muxer, sink)) {
        gst_object_unref(pipeline);
        return MEDIA_ERROR_GST_ELEMENT_LINK;
    }

    if (recorder->config.video_source != VIDEO_SOURCE_NONE) {
        GstElement *source = create_video_source(&recorder->config);
        GstElement *convert =
            gst_element_factory_make("videoconvert", "video_convert");
        GstElement *filter =
            gst_element_factory_make("capsfilter", "video_filter");
        GstElement *encoder = create_video_encoder(&recorder->config);
        GstElement *parser =
            create_video_parser(recorder->config.video_params.codec);
        GstElement *queue = gst_element_factory_make("queue", "video_queue");
        if (source == NULL || convert == NULL || filter == NULL ||
            encoder == NULL || parser == NULL || queue == NULL) {
            gst_object_unref(pipeline);
            return MEDIA_ERROR_RECORDER_NO_ENCODER;
        }

        GstCaps *caps = gst_caps_new_simple(
            "video/x-raw",
            "width", G_TYPE_INT, recorder->config.video_params.width,
            "height", G_TYPE_INT, recorder->config.video_params.height,
            "framerate", GST_TYPE_FRACTION,
            recorder->config.video_params.framerate_num,
            recorder->config.video_params.framerate_den,
            NULL);
        g_object_set(filter, "caps", caps, NULL);
        gst_caps_unref(caps);
        gst_bin_add_many(GST_BIN(pipeline), source, convert, filter,
                         encoder, parser, queue, NULL);
        if (!gst_element_link_many(source, convert, filter,
                                   encoder, parser, queue, NULL) ||
            !gst_element_link(queue, muxer)) {
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
    }

    if (recorder->config.audio_source != AUDIO_SOURCE_NONE) {
        GstElement *source = create_audio_source(&recorder->config);
        GstElement *convert =
            gst_element_factory_make("audioconvert", "audio_convert");
        GstElement *resample =
            gst_element_factory_make("audioresample", "audio_resample");
        GstElement *filter =
            gst_element_factory_make("capsfilter", "audio_filter");
        GstElement *encoder =
            create_audio_encoder(&recorder->config.audio_params);
        GstElement *parser =
            create_audio_parser(recorder->config.audio_params.codec);
        GstElement *queue = gst_element_factory_make("queue", "audio_queue");
        if (source == NULL || convert == NULL || resample == NULL ||
            filter == NULL || encoder == NULL || parser == NULL ||
            queue == NULL) {
            gst_object_unref(pipeline);
            return MEDIA_ERROR_RECORDER_NO_ENCODER;
        }

        GstCaps *caps = gst_caps_new_simple(
            "audio/x-raw",
            "rate", G_TYPE_INT, recorder->config.audio_params.sample_rate,
            "channels", G_TYPE_INT, recorder->config.audio_params.channels,
            NULL);
        g_object_set(filter, "caps", caps, NULL);
        gst_caps_unref(caps);
        gst_bin_add_many(GST_BIN(pipeline), source, convert, resample,
                         filter, encoder, parser, queue, NULL);
        if (!gst_element_link_many(source, convert, resample, filter,
                                   encoder, parser, queue, NULL) ||
            !gst_element_link(queue, muxer)) {
            gst_object_unref(pipeline);
            return MEDIA_ERROR_GST_ELEMENT_LINK;
        }
    }

    recorder->pipeline = pipeline;
    recorder->bus = gst_element_get_bus(pipeline);
    gst_bus_set_sync_handler(
        recorder->bus, recorder_bus_sync, recorder, NULL);
    return MEDIA_OK;
}

void recorder_config_init(RecorderConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->video_source = VIDEO_SOURCE_V4L2;
    strcpy(config->video_device, "/dev/video0");
    video_params_init(&config->video_params);
    config->audio_source = AUDIO_SOURCE_ALSA;
    strcpy(config->audio_device, "default");
    audio_params_init(&config->audio_params);
    strcpy(config->output_file, "output.mp4");
    config->container = CONTAINER_MP4;
    config->low_latency = 1;
    config->buffer_size = 1024 * 1024;
    config->enable_hardware_encode = 0;
    config->realtime = 1;
}

void recorder_callbacks_init(RecorderCallbacks *callbacks) {
    if (callbacks != NULL) memset(callbacks, 0, sizeof(*callbacks));
}

MediaRecorder *recorder_create(const RecorderConfig *config) {
    gst_init(NULL, NULL);
    MediaRecorder *recorder = g_new0(MediaRecorder, 1);
    if (recorder == NULL) return NULL;
    g_mutex_init(&recorder->mutex);
    g_cond_init(&recorder->condition);
    if (config != NULL) recorder->config = *config;
    else recorder_config_init(&recorder->config);
    recorder->state = MEDIA_STATE_NULL;
    return recorder;
}

void recorder_destroy(MediaRecorder *recorder) {
    if (recorder == NULL) return;
    if (recorder->pipeline != NULL) {
        recorder_stop(recorder);
    }
    g_cond_clear(&recorder->condition);
    g_mutex_clear(&recorder->mutex);
    g_free(recorder);
}

MediaErrorCode recorder_set_callbacks(
    MediaRecorder *recorder, const RecorderCallbacks *callbacks) {
    if (recorder == NULL) return MEDIA_ERROR_INVALID_PARAM;
    g_mutex_lock(&recorder->mutex);
    if (callbacks != NULL) recorder->callbacks = *callbacks;
    else recorder_callbacks_init(&recorder->callbacks);
    g_mutex_unlock(&recorder->mutex);
    return MEDIA_OK;
}

MediaErrorCode recorder_start(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->config.output_file[0] == '\0') {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (recorder->pipeline == NULL) {
        MediaErrorCode result = build_pipeline(recorder);
        if (result != MEDIA_OK) return result;
    }
    g_mutex_lock(&recorder->mutex);
    recorder->eos_received = FALSE;
    recorder->error_received = FALSE;
    g_mutex_unlock(&recorder->mutex);
    if (gst_element_set_state(recorder->pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    recorder->start_time_us = g_get_monotonic_time();
    recorder_notify_state(recorder, MEDIA_STATE_PLAYING);
    g_atomic_int_set(&recorder->progress_thread_running, TRUE);
    recorder->progress_thread = g_thread_new(
        "stage2-recorder-progress", progress_thread_main, recorder);
    return MEDIA_OK;
}

MediaErrorCode recorder_pause(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->pipeline == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    if (gst_element_set_state(recorder->pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    recorder_notify_state(recorder, MEDIA_STATE_PAUSED);
    return MEDIA_OK;
}

MediaErrorCode recorder_resume(MediaRecorder *recorder) {
    if (recorder == NULL || recorder->pipeline == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    if (gst_element_set_state(recorder->pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    recorder_notify_state(recorder, MEDIA_STATE_PLAYING);
    return MEDIA_OK;
}

MediaErrorCode recorder_stop(MediaRecorder *recorder) {
    if (recorder == NULL) return MEDIA_ERROR_INVALID_PARAM;
    if (recorder->pipeline == NULL) return MEDIA_OK;

    g_atomic_int_set(&recorder->progress_thread_running, FALSE);
    if (recorder->progress_thread != NULL) {
        g_thread_join(recorder->progress_thread);
        recorder->progress_thread = NULL;
    }
    g_mutex_lock(&recorder->mutex);
    gboolean eos_received = recorder->eos_received;
    g_mutex_unlock(&recorder->mutex);
    if (!eos_received) {
        gst_element_send_event(recorder->pipeline, gst_event_new_eos());
    }
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    g_mutex_lock(&recorder->mutex);
    while (!recorder->eos_received && !recorder->error_received) {
        if (!g_cond_wait_until(
                &recorder->condition, &recorder->mutex, deadline)) {
            break;
        }
    }
    gboolean had_error = recorder->error_received;
    g_mutex_unlock(&recorder->mutex);

    recorder->duration =
        (g_get_monotonic_time() - recorder->start_time_us) * 1000;
    struct stat st;
    if (stat(recorder->config.output_file, &st) == 0) {
        recorder->file_size = st.st_size;
    }
    RecorderCallbacks callbacks;
    g_mutex_lock(&recorder->mutex);
    callbacks = recorder->callbacks;
    g_mutex_unlock(&recorder->mutex);
    if (callbacks.on_progress != NULL) {
        callbacks.on_progress(
            recorder, recorder->duration, recorder->file_size,
            callbacks.user_data);
    }
    destroy_pipeline(recorder);
    recorder_notify_state(recorder,
                          had_error ? MEDIA_STATE_ERROR : MEDIA_STATE_READY);
    return had_error ? MEDIA_ERROR_GST_MESSAGE_ERROR : MEDIA_OK;
}

MediaState recorder_get_state(MediaRecorder *recorder) {
    if (recorder == NULL) return MEDIA_STATE_NULL;
    g_mutex_lock(&recorder->mutex);
    MediaState state = recorder->state;
    g_mutex_unlock(&recorder->mutex);
    return state;
}

int64_t recorder_get_duration(MediaRecorder *recorder) {
    if (recorder == NULL) return 0;
    MediaState state = recorder_get_state(recorder);
    if (state == MEDIA_STATE_PLAYING || state == MEDIA_STATE_PAUSED) {
        return (g_get_monotonic_time() - recorder->start_time_us) * 1000;
    }
    return recorder->duration;
}

int64_t recorder_get_file_size(MediaRecorder *recorder) {
    if (recorder == NULL) return 0;
    struct stat st;
    if (stat(recorder->config.output_file, &st) == 0) {
        recorder->file_size = st.st_size;
    }
    return recorder->file_size;
}

MediaErrorCode recorder_set_video_params(
    MediaRecorder *recorder, const VideoParams *params) {
    if (recorder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (recorder_get_state(recorder) == MEDIA_STATE_PLAYING ||
        recorder_get_state(recorder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    recorder->config.video_params = *params;
    return MEDIA_OK;
}

MediaErrorCode recorder_set_audio_params(
    MediaRecorder *recorder, const AudioParams *params) {
    if (recorder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (recorder_get_state(recorder) == MEDIA_STATE_PLAYING ||
        recorder_get_state(recorder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    recorder->config.audio_params = *params;
    return MEDIA_OK;
}

MediaErrorCode recorder_set_output_file(
    MediaRecorder *recorder, const char *file_path) {
    if (recorder == NULL || file_path == NULL || file_path[0] == '\0') {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (recorder_get_state(recorder) == MEDIA_STATE_PLAYING ||
        recorder_get_state(recorder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    g_strlcpy(recorder->config.output_file, file_path,
              sizeof(recorder->config.output_file));
    return MEDIA_OK;
}

int recorder_enum_video_devices(char devices[][256], int max_count) {
    if (devices == NULL || max_count <= 0) return 0;
    DIR *dir = opendir("/dev");
    if (dir == NULL) return 0;
    int count = 0;
    struct dirent *entry;
    while (count < max_count && (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "video", 5) == 0) {
            g_snprintf(devices[count], 256, "/dev/%.250s", entry->d_name);
            ++count;
        }
    }
    closedir(dir);
    return count;
}

int recorder_enum_audio_devices(char devices[][256], int max_count) {
    if (devices == NULL || max_count <= 0) return 0;
    g_strlcpy(devices[0], "default", 256);
    return 1;
}
