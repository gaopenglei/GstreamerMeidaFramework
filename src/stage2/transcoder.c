#include "transcoder.h"

#include "learning_stage.h"
#include "logger.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M2)
#error "The stage 2 transcoder requires MEDIA_FRAMEWORK_STAGE >= 2"
#endif

struct MediaTranscoder {
    TranscoderConfig config;
    TranscoderCallbacks callbacks;
    GstElement *pipeline;
    GstBus *bus;
    GMutex mutex;
    GCond condition;
    GThread *progress_thread;
    gint progress_thread_running;
    MediaState state;
    gboolean eos_received;
    gboolean error_received;
    MediaInfo input_info;
    TranscodeProgress progress;
    gint64 started_us;
};

static gpointer transcoder_progress_thread(gpointer data) {
    MediaTranscoder *transcoder = (MediaTranscoder *)data;
    while (g_atomic_int_get(&transcoder->progress_thread_running)) {
        g_usleep(200000);
        if (!g_atomic_int_get(&transcoder->progress_thread_running)) break;
        TranscodeProgress progress;
        transcoder_get_progress(transcoder, &progress);
    }
    return NULL;
}

static void transcoder_notify_state(MediaTranscoder *transcoder,
                                    MediaState state) {
    TranscoderCallbacks callbacks;
    g_mutex_lock(&transcoder->mutex);
    transcoder->state = state;
    callbacks = transcoder->callbacks;
    g_mutex_unlock(&transcoder->mutex);
    if (callbacks.on_state_changed != NULL) {
        callbacks.on_state_changed(transcoder, state, callbacks.user_data);
    }
}

static GstBusSyncReply transcoder_bus_sync(GstBus *bus,
                                           GstMessage *message,
                                           gpointer data) {
    (void)bus;
    MediaTranscoder *transcoder = (MediaTranscoder *)data;

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = NULL;
        gchar *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        LOG_ERROR("Stage 2 transcoder error: %s (%s)",
                  error->message, debug != NULL ? debug : "no details");

        TranscoderCallbacks callbacks;
        g_mutex_lock(&transcoder->mutex);
        transcoder->error_received = TRUE;
        callbacks = transcoder->callbacks;
        g_cond_broadcast(&transcoder->condition);
        g_mutex_unlock(&transcoder->mutex);
        if (callbacks.on_error != NULL) {
            callbacks.on_error(transcoder, MEDIA_ERROR_GST_MESSAGE_ERROR,
                               error->message, callbacks.user_data);
        }
        g_clear_error(&error);
        g_free(debug);
        g_atomic_int_set(&transcoder->progress_thread_running, FALSE);
        transcoder_notify_state(transcoder, MEDIA_STATE_ERROR);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        TranscoderCallbacks callbacks;
        g_mutex_lock(&transcoder->mutex);
        transcoder->eos_received = TRUE;
        transcoder->progress.progress = 100.0;
        callbacks = transcoder->callbacks;
        g_cond_broadcast(&transcoder->condition);
        g_mutex_unlock(&transcoder->mutex);
        if (callbacks.on_progress != NULL) {
            callbacks.on_progress(
                transcoder, &transcoder->progress, callbacks.user_data);
        }
        g_atomic_int_set(&transcoder->progress_thread_running, FALSE);
        transcoder_notify_state(transcoder, MEDIA_STATE_EOS);
    }

    return GST_BUS_DROP;
}

static const char *muxer_name(ContainerFormat format) {
    if (format == CONTAINER_MP4) return "mp4mux";
    if (format == CONTAINER_MKV) return "matroskamux";
    if (format == CONTAINER_WEBM) return "webmmux";
    if (format == CONTAINER_TS) return "mpegtsmux";
    if (format == CONTAINER_FLV) return "flvmux";
    return NULL;
}

static gchar *quote_parse_value(const gchar *value) {
    gchar *escaped = g_strescape(value, NULL);
    gchar *quoted = g_strdup_printf("\"%s\"", escaped);
    g_free(escaped);
    return quoted;
}

static gchar *video_encoder_chain(const TranscoderConfig *config) {
    int bitrate_kbps = MAX(config->video_params.bitrate / 1000, 1);
    int gop = MAX(config->video_params.gop_size, 1);
    if (config->video_params.codec == VIDEO_CODEC_H264) {
        return g_strdup_printf(
            "videoconvert ! x264enc bitrate=%d key-int-max=%d%s ! "
            "h264parse config-interval=-1",
            bitrate_kbps, gop,
            config->low_latency ? " tune=zerolatency" : "");
    }
    if (config->video_params.codec == VIDEO_CODEC_H265) {
        return g_strdup_printf(
            "videoconvert ! x265enc bitrate=%d key-int-max=%d ! h265parse",
            bitrate_kbps, gop);
    }
    if (config->video_params.codec == VIDEO_CODEC_VP8) {
        return g_strdup_printf(
            "videoconvert ! vp8enc target-bitrate=%d",
            config->video_params.bitrate);
    }
    if (config->video_params.codec == VIDEO_CODEC_VP9) {
        return g_strdup_printf(
            "videoconvert ! vp9enc target-bitrate=%d",
            config->video_params.bitrate);
    }
    return NULL;
}

static gchar *audio_encoder_chain(const TranscoderConfig *config) {
    if (config->audio_params.codec == AUDIO_CODEC_AAC) {
        return g_strdup_printf(
            "audioconvert ! audioresample ! avenc_aac bitrate=%d ! aacparse",
            config->audio_params.bitrate);
    }
    if (config->audio_params.codec == AUDIO_CODEC_OPUS) {
        return g_strdup_printf(
            "audioconvert ! audioresample ! opusenc bitrate=%d ! opusparse",
            config->audio_params.bitrate);
    }
    if (config->audio_params.codec == AUDIO_CODEC_VORBIS) {
        return g_strdup_printf(
            "audioconvert ! audioresample ! vorbisenc bitrate=%d",
            config->audio_params.bitrate);
    }
    if (config->audio_params.codec == AUDIO_CODEC_FLAC) {
        return g_strdup("audioconvert ! audioresample ! flacenc");
    }
    if (config->audio_params.codec == AUDIO_CODEC_MP3) {
        return g_strdup_printf(
            "audioconvert ! audioresample ! lamemp3enc bitrate=%d ! "
            "mpegaudioparse",
            MAX(config->audio_params.bitrate / 1000, 8));
    }
    return NULL;
}

static gchar *build_pipeline_description(const TranscoderConfig *config) {
    const char *muxer = muxer_name(config->output_container);
    if (muxer == NULL) return NULL;

    gchar *input = quote_parse_value(config->input_file);
    gchar *output = quote_parse_value(config->output_file);
    gchar *uri_value = gst_filename_to_uri(config->input_file, NULL);
    if (uri_value == NULL) {
        g_free(input);
        g_free(output);
        return NULL;
    }
    gchar *uri = quote_parse_value(uri_value);
    g_free(uri_value);

    GString *description = g_string_new(NULL);
    g_string_append_printf(description,
                           "%s name=mux ! filesink location=%s ",
                           muxer, output);

    gchar *video_chain = config->video_transcode
        ? video_encoder_chain(config) : NULL;
    gchar *audio_chain = config->audio_transcode
        ? audio_encoder_chain(config) : NULL;
    if ((config->video_transcode && video_chain == NULL) ||
        (config->audio_transcode && audio_chain == NULL)) {
        g_string_free(description, TRUE);
        g_free(video_chain);
        g_free(audio_chain);
        g_free(input);
        g_free(output);
        g_free(uri);
        return NULL;
    }

    if (config->video_transcode && config->audio_transcode) {
        g_string_append_printf(
            description,
            "uridecodebin uri=%s name=decode "
            "decode. ! video/x-raw ! queue ! %s ! mux. "
            "decode. ! audio/x-raw ! queue ! %s ! mux. ",
            uri, video_chain, audio_chain);
    } else if (!config->video_transcode && !config->audio_transcode) {
        g_string_append_printf(
            description,
            "filesrc location=%s ! parsebin name=parse "
            "parse. ! capsfilter caps=\"video/x-h264;video/x-h265;"
            "video/x-vp8;video/x-vp9;video/mpeg\" ! queue ! mux. "
            "parse. ! capsfilter caps=\"audio/mpeg;audio/x-opus;"
            "audio/x-vorbis;audio/x-flac\" ! queue ! mux. ",
            input);
    } else if (config->video_transcode) {
        g_string_append_printf(
            description,
            "uridecodebin uri=%s name=decode "
            "decode. ! video/x-raw ! queue ! %s ! mux. "
            "filesrc location=%s ! parsebin name=parse "
            "parse. ! capsfilter caps=\"audio/mpeg;audio/x-opus;"
            "audio/x-vorbis;audio/x-flac\" ! queue ! mux. ",
            uri, video_chain, input);
    } else {
        g_string_append_printf(
            description,
            "filesrc location=%s ! parsebin name=parse "
            "parse. ! capsfilter caps=\"video/x-h264;video/x-h265;"
            "video/x-vp8;video/x-vp9;video/mpeg\" ! queue ! mux. "
            "uridecodebin uri=%s name=decode "
            "decode. ! audio/x-raw ! queue ! %s ! mux. ",
            input, uri, audio_chain);
    }

    g_free(video_chain);
    g_free(audio_chain);

    g_free(input);
    g_free(output);
    g_free(uri);
    return g_string_free(description, FALSE);
}

static void discover_input(MediaTranscoder *transcoder) {
    GError *error = NULL;
    GstDiscoverer *discoverer = gst_discoverer_new(5 * GST_SECOND, &error);
    if (discoverer == NULL) {
        g_clear_error(&error);
        return;
    }
    gchar *uri = gst_filename_to_uri(transcoder->config.input_file, NULL);
    GstDiscovererInfo *info = uri != NULL
        ? gst_discoverer_discover_uri(discoverer, uri, &error)
        : NULL;
    if (info != NULL && error == NULL &&
        gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK) {
        media_info_init(&transcoder->input_info);
        g_strlcpy(transcoder->input_info.uri, uri,
                  sizeof(transcoder->input_info.uri));
        transcoder->input_info.duration =
            gst_discoverer_info_get_duration(info);
        transcoder->input_info.seekable =
            gst_discoverer_info_get_seekable(info);
        GList *videos = gst_discoverer_info_get_video_streams(info);
        GList *audios = gst_discoverer_info_get_audio_streams(info);
        transcoder->input_info.has_video = videos != NULL;
        transcoder->input_info.has_audio = audios != NULL;
        gst_discoverer_stream_info_list_free(videos);
        gst_discoverer_stream_info_list_free(audios);
        gst_discoverer_info_unref(info);
    }
    g_clear_error(&error);
    g_free(uri);
    g_object_unref(discoverer);

    struct stat st;
    if (stat(transcoder->config.input_file, &st) == 0) {
        transcoder->progress.bytes_total = st.st_size;
    }
}

static void destroy_pipeline(MediaTranscoder *transcoder) {
    if (transcoder->pipeline == NULL) return;
    g_atomic_int_set(&transcoder->progress_thread_running, FALSE);
    if (transcoder->progress_thread != NULL) {
        g_thread_join(transcoder->progress_thread);
        transcoder->progress_thread = NULL;
    }
    gst_bus_set_sync_handler(transcoder->bus, NULL, NULL, NULL);
    gst_element_set_state(transcoder->pipeline, GST_STATE_NULL);
    gst_object_unref(transcoder->bus);
    gst_object_unref(transcoder->pipeline);
    transcoder->bus = NULL;
    transcoder->pipeline = NULL;
}

static MediaErrorCode build_pipeline(MediaTranscoder *transcoder) {
    gchar *description = build_pipeline_description(&transcoder->config);
    if (description == NULL) {
        return MEDIA_ERROR_CODEC_NOT_SUPPORTED;
    }
    LOG_DEBUG("Stage 2 transcode pipeline: %s", description);

    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (pipeline == NULL || error != NULL) {
        if (error != NULL) {
            LOG_ERROR("Failed to construct transcode pipeline: %s",
                      error->message);
        }
        g_clear_error(&error);
        if (pipeline != NULL) gst_object_unref(pipeline);
        return MEDIA_ERROR_TRANSCODER_CREATE_PIPELINE;
    }

    transcoder->pipeline = pipeline;
    transcoder->bus = gst_element_get_bus(pipeline);
    gst_bus_set_sync_handler(
        transcoder->bus, transcoder_bus_sync, transcoder, NULL);
    return MEDIA_OK;
}

void transcoder_config_init(TranscoderConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->output_container = CONTAINER_MP4;
    config->video_transcode = 1;
    video_params_init(&config->video_params);
    config->video_params.codec = VIDEO_CODEC_H265;
    config->audio_transcode = 1;
    audio_params_init(&config->audio_params);
    config->low_latency = 0;
    config->enable_hardware = 0;
    config->threads = 0;
    config->copy_ts = 1;
    config->end_time = -1;
}

void transcoder_callbacks_init(TranscoderCallbacks *callbacks) {
    if (callbacks != NULL) memset(callbacks, 0, sizeof(*callbacks));
}

MediaTranscoder *transcoder_create(const TranscoderConfig *config) {
    gst_init(NULL, NULL);
    MediaTranscoder *transcoder = g_new0(MediaTranscoder, 1);
    if (transcoder == NULL) return NULL;
    g_mutex_init(&transcoder->mutex);
    g_cond_init(&transcoder->condition);
    if (config != NULL) transcoder->config = *config;
    else transcoder_config_init(&transcoder->config);
    media_info_init(&transcoder->input_info);
    transcoder->state = MEDIA_STATE_NULL;
    discover_input(transcoder);
    return transcoder;
}

void transcoder_destroy(MediaTranscoder *transcoder) {
    if (transcoder == NULL) return;
    if (transcoder->pipeline != NULL) destroy_pipeline(transcoder);
    g_cond_clear(&transcoder->condition);
    g_mutex_clear(&transcoder->mutex);
    g_free(transcoder);
}

MediaErrorCode transcoder_set_callbacks(
    MediaTranscoder *transcoder, const TranscoderCallbacks *callbacks) {
    if (transcoder == NULL) return MEDIA_ERROR_INVALID_PARAM;
    g_mutex_lock(&transcoder->mutex);
    if (callbacks != NULL) transcoder->callbacks = *callbacks;
    else transcoder_callbacks_init(&transcoder->callbacks);
    g_mutex_unlock(&transcoder->mutex);
    return MEDIA_OK;
}

MediaErrorCode transcoder_start(MediaTranscoder *transcoder) {
    if (transcoder == NULL ||
        transcoder->config.input_file[0] == '\0' ||
        transcoder->config.output_file[0] == '\0') {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (transcoder->pipeline == NULL) {
        MediaErrorCode result = build_pipeline(transcoder);
        if (result != MEDIA_OK) return result;
    }
    g_mutex_lock(&transcoder->mutex);
    transcoder->eos_received = FALSE;
    transcoder->error_received = FALSE;
    g_mutex_unlock(&transcoder->mutex);

    if (gst_element_set_state(transcoder->pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    gst_element_get_state(
        transcoder->pipeline, NULL, NULL, 5 * GST_SECOND);

    gint64 duration = 0;
    if (gst_element_query_duration(
            transcoder->pipeline, GST_FORMAT_TIME, &duration)) {
        transcoder->progress.duration = duration;
    } else {
        transcoder->progress.duration = transcoder->input_info.duration;
    }

    if (transcoder->config.start_time > 0 ||
        transcoder->config.end_time > 0) {
        GstSeekType end_type = transcoder->config.end_time > 0
            ? GST_SEEK_TYPE_SET : GST_SEEK_TYPE_NONE;
        gint64 end = transcoder->config.end_time > 0
            ? transcoder->config.end_time : -1;
        gboolean seek_ok = TRUE;
        if (transcoder->config.video_transcode ||
            transcoder->config.audio_transcode) {
            GstElement *decode = gst_bin_get_by_name(
                GST_BIN(transcoder->pipeline), "decode");
            seek_ok = decode != NULL && gst_element_seek(
                decode, 1.0, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                GST_SEEK_TYPE_SET, MAX(transcoder->config.start_time, 0),
                end_type, end);
            if (decode != NULL) gst_object_unref(decode);
        }
        if (seek_ok &&
            (!transcoder->config.video_transcode ||
             !transcoder->config.audio_transcode)) {
            GstElement *parse = gst_bin_get_by_name(
                GST_BIN(transcoder->pipeline), "parse");
            seek_ok = parse != NULL && gst_element_seek(
                parse, 1.0, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                GST_SEEK_TYPE_SET, MAX(transcoder->config.start_time, 0),
                end_type, end);
            if (parse != NULL) gst_object_unref(parse);
        }
        if (!seek_ok) {
            return MEDIA_ERROR_IO_SEEK_FAILED;
        }
    }

    transcoder->started_us = g_get_monotonic_time();
    transcoder_notify_state(transcoder, MEDIA_STATE_PLAYING);
    g_atomic_int_set(&transcoder->progress_thread_running, TRUE);
    transcoder->progress_thread = g_thread_new(
        "stage2-transcode-progress",
        transcoder_progress_thread,
        transcoder);
    if (gst_element_set_state(transcoder->pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        g_atomic_int_set(&transcoder->progress_thread_running, FALSE);
        g_thread_join(transcoder->progress_thread);
        transcoder->progress_thread = NULL;
        transcoder_notify_state(transcoder, MEDIA_STATE_ERROR);
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    return MEDIA_OK;
}

MediaErrorCode transcoder_pause(MediaTranscoder *transcoder) {
    if (transcoder == NULL || transcoder->pipeline == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    if (gst_element_set_state(transcoder->pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    transcoder_notify_state(transcoder, MEDIA_STATE_PAUSED);
    return MEDIA_OK;
}

MediaErrorCode transcoder_resume(MediaTranscoder *transcoder) {
    if (transcoder == NULL || transcoder->pipeline == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    if (gst_element_set_state(transcoder->pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        return MEDIA_ERROR_GST_STATE_CHANGE;
    }
    transcoder_notify_state(transcoder, MEDIA_STATE_PLAYING);
    return MEDIA_OK;
}

MediaErrorCode transcoder_stop(MediaTranscoder *transcoder) {
    if (transcoder == NULL) return MEDIA_ERROR_INVALID_PARAM;
    if (transcoder->pipeline == NULL) return MEDIA_OK;
    MediaState state = transcoder_get_state(transcoder);
    if (state == MEDIA_STATE_PLAYING || state == MEDIA_STATE_PAUSED) {
        gst_element_send_event(transcoder->pipeline, gst_event_new_eos());
        gint64 deadline =
            g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
        g_mutex_lock(&transcoder->mutex);
        while (!transcoder->eos_received && !transcoder->error_received) {
            if (!g_cond_wait_until(
                    &transcoder->condition,
                    &transcoder->mutex,
                    deadline)) {
                break;
            }
        }
        state = transcoder->error_received
            ? MEDIA_STATE_ERROR : transcoder->state;
        g_mutex_unlock(&transcoder->mutex);
    }
    destroy_pipeline(transcoder);
    if (state != MEDIA_STATE_ERROR) {
        transcoder_notify_state(transcoder, MEDIA_STATE_READY);
    }
    return state == MEDIA_STATE_ERROR
        ? MEDIA_ERROR_GST_MESSAGE_ERROR : MEDIA_OK;
}

MediaState transcoder_get_state(MediaTranscoder *transcoder) {
    if (transcoder == NULL) return MEDIA_STATE_NULL;
    g_mutex_lock(&transcoder->mutex);
    MediaState state = transcoder->state;
    g_mutex_unlock(&transcoder->mutex);
    return state;
}

MediaErrorCode transcoder_get_progress(
    MediaTranscoder *transcoder, TranscodeProgress *progress) {
    if (transcoder == NULL || progress == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    MediaState state = transcoder_get_state(transcoder);
    if (transcoder->pipeline != NULL && state != MEDIA_STATE_EOS) {
        gint64 position = 0;
        gst_element_query_position(
            transcoder->pipeline, GST_FORMAT_TIME, &position);
        g_mutex_lock(&transcoder->mutex);
        transcoder->progress.position = position;
        if (transcoder->progress.duration > 0) {
            transcoder->progress.progress =
                CLAMP((double)position /
                      (double)transcoder->progress.duration * 100.0,
                      0.0, 100.0);
        }
        transcoder->progress.elapsed_time =
            (g_get_monotonic_time() - transcoder->started_us) / 1000;
        if (transcoder->progress.elapsed_time > 0) {
            transcoder->progress.speed =
                (double)position /
                ((double)transcoder->progress.elapsed_time * GST_MSECOND);
        }
        transcoder->progress.bytes_processed =
            (int64_t)(transcoder->progress.bytes_total *
                      transcoder->progress.progress / 100.0);
        g_mutex_unlock(&transcoder->mutex);
    }
    g_mutex_lock(&transcoder->mutex);
    *progress = transcoder->progress;
    TranscoderCallbacks callbacks = transcoder->callbacks;
    g_mutex_unlock(&transcoder->mutex);
    if (callbacks.on_progress != NULL) {
        callbacks.on_progress(transcoder, progress, callbacks.user_data);
    }
    return MEDIA_OK;
}

MediaErrorCode transcoder_get_input_info(
    MediaTranscoder *transcoder, MediaInfo *info) {
    if (transcoder == NULL || info == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    *info = transcoder->input_info;
    return MEDIA_OK;
}

MediaErrorCode transcoder_set_video_params(
    MediaTranscoder *transcoder, const VideoParams *params) {
    if (transcoder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (transcoder_get_state(transcoder) == MEDIA_STATE_PLAYING ||
        transcoder_get_state(transcoder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    transcoder->config.video_params = *params;
    return MEDIA_OK;
}

MediaErrorCode transcoder_set_audio_params(
    MediaTranscoder *transcoder, const AudioParams *params) {
    if (transcoder == NULL || params == NULL) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (transcoder_get_state(transcoder) == MEDIA_STATE_PLAYING ||
        transcoder_get_state(transcoder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    transcoder->config.audio_params = *params;
    return MEDIA_OK;
}

MediaErrorCode transcoder_set_range(
    MediaTranscoder *transcoder, int64_t start_time, int64_t end_time) {
    if (transcoder == NULL || start_time < 0 ||
        (end_time >= 0 && end_time <= start_time)) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    if (transcoder_get_state(transcoder) == MEDIA_STATE_PLAYING ||
        transcoder_get_state(transcoder) == MEDIA_STATE_PAUSED) {
        return MEDIA_ERROR_BUSY;
    }
    transcoder->config.start_time = start_time;
    transcoder->config.end_time = end_time;
    return MEDIA_OK;
}
