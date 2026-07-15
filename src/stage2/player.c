#include "player.h"

#include "learning_stage.h"
#include "logger.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/videooverlay.h>
#include <stdlib.h>
#include <string.h>

#if !MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M2)
#error "The stage 2 player requires MEDIA_FRAMEWORK_STAGE >= 2"
#endif

struct MediaPlayer {
    PlayerConfig config;
    PlayerCallbacks callbacks;
    GstElement *playbin;
    GstElement *video_sink;
    GstElement *audio_sink;
    GstBus *bus;
    GMutex mutex;
    GThread *position_thread;
    gint position_thread_running;
    MediaState state;
    MediaInfo media_info;
    double volume;
    int mute;
    double rate;
    uintptr_t window_id;
};

static gpointer position_thread_main(gpointer data) {
    MediaPlayer *player = (MediaPlayer *)data;
    while (g_atomic_int_get(&player->position_thread_running)) {
        gint64 interval_ms = MAX(
            player->config.position_update_interval, 20);
        g_usleep((gulong)interval_ms * 1000U);
        if (!g_atomic_int_get(&player->position_thread_running) ||
            player_get_state(player) != MEDIA_STATE_PLAYING) {
            continue;
        }

        gint64 position = player_get_position(player);
        PlayerCallbacks callbacks;
        g_mutex_lock(&player->mutex);
        callbacks = player->callbacks;
        g_mutex_unlock(&player->mutex);
        if (callbacks.on_position != NULL) {
            callbacks.on_position(
                player, position, callbacks.user_data);
        }
    }
    return NULL;
}

static VideoCodec video_codec_from_caps(const GstCaps *caps) {
    if (caps == NULL || gst_caps_is_empty(caps)) {
        return VIDEO_CODEC_NONE;
    }
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(s);
    if (g_str_equal(name, "video/x-h264")) return VIDEO_CODEC_H264;
    if (g_str_equal(name, "video/x-h265")) return VIDEO_CODEC_H265;
    if (g_str_equal(name, "video/x-vp8")) return VIDEO_CODEC_VP8;
    if (g_str_equal(name, "video/x-vp9")) return VIDEO_CODEC_VP9;
    if (g_str_equal(name, "video/x-av1")) return VIDEO_CODEC_AV1;
    if (g_str_equal(name, "video/mpeg")) {
        gint version = 0;
        gst_structure_get_int(s, "mpegversion", &version);
        return version == 2 ? VIDEO_CODEC_MPEG2 : VIDEO_CODEC_MPEG4;
    }
    return VIDEO_CODEC_NONE;
}

static AudioCodec audio_codec_from_caps(const GstCaps *caps) {
    if (caps == NULL || gst_caps_is_empty(caps)) {
        return AUDIO_CODEC_NONE;
    }
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(s);
    if (g_str_equal(name, "audio/x-opus")) return AUDIO_CODEC_OPUS;
    if (g_str_equal(name, "audio/x-vorbis")) return AUDIO_CODEC_VORBIS;
    if (g_str_equal(name, "audio/x-flac")) return AUDIO_CODEC_FLAC;
    if (g_str_equal(name, "audio/x-raw")) return AUDIO_CODEC_PCM;
    if (g_str_equal(name, "audio/mpeg")) {
        gint version = 0;
        gst_structure_get_int(s, "mpegversion", &version);
        return version == 1 ? AUDIO_CODEC_MP3 : AUDIO_CODEC_AAC;
    }
    return AUDIO_CODEC_NONE;
}

static void player_notify_state(MediaPlayer *player, MediaState state) {
    PlayerCallbacks callbacks;
    g_mutex_lock(&player->mutex);
    player->state = state;
    callbacks = player->callbacks;
    g_mutex_unlock(&player->mutex);
    if (callbacks.on_state_changed != NULL) {
        callbacks.on_state_changed(player, state, callbacks.user_data);
    }
}

static GstBusSyncReply player_bus_sync(GstBus *bus,
                                       GstMessage *message,
                                       gpointer data) {
    (void)bus;
    MediaPlayer *player = (MediaPlayer *)data;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *error = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &error, &debug);
            LOG_ERROR("Stage 2 player error: %s (%s)",
                      error->message, debug != NULL ? debug : "no details");

            PlayerCallbacks callbacks;
            g_mutex_lock(&player->mutex);
            callbacks = player->callbacks;
            g_mutex_unlock(&player->mutex);
            if (callbacks.on_error != NULL) {
                callbacks.on_error(player, MEDIA_ERROR_GST_MESSAGE_ERROR,
                                   error->message, callbacks.user_data);
            }
            g_clear_error(&error);
            g_free(debug);
            player_notify_state(player, MEDIA_STATE_ERROR);
            break;
        }
        case GST_MESSAGE_EOS: {
            PlayerCallbacks callbacks;
            g_mutex_lock(&player->mutex);
            callbacks = player->callbacks;
            g_mutex_unlock(&player->mutex);
            if (callbacks.on_event != NULL) {
                MediaEvent event = {
                    .type = MEDIA_EVENT_EOS,
                    .code = 0,
                    .message = "End of stream"
                };
                callbacks.on_event(&event, callbacks.user_data);
            }
            player_notify_state(player, MEDIA_STATE_EOS);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(player->playbin)) {
                GstState old_state;
                GstState new_state;
                GstState pending_state;
                gst_message_parse_state_changed(
                    message, &old_state, &new_state, &pending_state);
                (void)old_state;
                (void)pending_state;
                MediaState state = MEDIA_STATE_NULL;
                if (new_state == GST_STATE_READY) state = MEDIA_STATE_READY;
                else if (new_state == GST_STATE_PAUSED) state = MEDIA_STATE_PAUSED;
                else if (new_state == GST_STATE_PLAYING) state = MEDIA_STATE_PLAYING;
                player_notify_state(player, state);
            }
            break;
        default:
            break;
    }

    return GST_BUS_DROP;
}

static void discover_media(MediaPlayer *player, const gchar *uri) {
    GError *error = NULL;
    GstDiscoverer *discoverer = gst_discoverer_new(5 * GST_SECOND, &error);
    if (discoverer == NULL) {
        g_clear_error(&error);
        return;
    }

    GstDiscovererInfo *info = gst_discoverer_discover_uri(discoverer, uri, &error);
    if (info == NULL || error != NULL ||
        gst_discoverer_info_get_result(info) != GST_DISCOVERER_OK) {
        g_clear_error(&error);
        if (info != NULL) gst_discoverer_info_unref(info);
        g_object_unref(discoverer);
        return;
    }

    player->media_info.duration = gst_discoverer_info_get_duration(info);
    player->media_info.seekable = gst_discoverer_info_get_seekable(info);

    GList *videos = gst_discoverer_info_get_video_streams(info);
    if (videos != NULL) {
        GstDiscovererVideoInfo *video = GST_DISCOVERER_VIDEO_INFO(videos->data);
        player->media_info.has_video = 1;
        player->media_info.video_params.width =
            (int)gst_discoverer_video_info_get_width(video);
        player->media_info.video_params.height =
            (int)gst_discoverer_video_info_get_height(video);
        player->media_info.video_params.framerate_num =
            (int)gst_discoverer_video_info_get_framerate_num(video);
        player->media_info.video_params.framerate_den =
            (int)gst_discoverer_video_info_get_framerate_denom(video);
        player->media_info.video_params.bitrate =
            (int)gst_discoverer_video_info_get_bitrate(video);
        GstCaps *caps = gst_discoverer_stream_info_get_caps(
            GST_DISCOVERER_STREAM_INFO(video));
        player->media_info.video_params.codec = video_codec_from_caps(caps);
        if (caps != NULL) gst_caps_unref(caps);
    }
    gst_discoverer_stream_info_list_free(videos);

    GList *audios = gst_discoverer_info_get_audio_streams(info);
    if (audios != NULL) {
        GstDiscovererAudioInfo *audio = GST_DISCOVERER_AUDIO_INFO(audios->data);
        player->media_info.has_audio = 1;
        player->media_info.audio_params.sample_rate =
            (int)gst_discoverer_audio_info_get_sample_rate(audio);
        player->media_info.audio_params.channels =
            (int)gst_discoverer_audio_info_get_channels(audio);
        player->media_info.audio_params.bitrate =
            (int)gst_discoverer_audio_info_get_bitrate(audio);
        GstCaps *caps = gst_discoverer_stream_info_get_caps(
            GST_DISCOVERER_STREAM_INFO(audio));
        player->media_info.audio_params.codec = audio_codec_from_caps(caps);
        if (caps != NULL) gst_caps_unref(caps);
    }
    gst_discoverer_stream_info_list_free(audios);

    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);
}

void player_config_init(PlayerConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    strcpy(config->video_sink, "autovideosink");
    strcpy(config->audio_sink, "autoaudiosink");
    config->low_latency = 1;
    config->buffer_duration = 500;
    config->buffer_size = 2 * 1024 * 1024;
    config->enable_hardware_decode = 1;
    config->sync = 1;
    config->position_update_interval = 100;
}

void player_callbacks_init(PlayerCallbacks *callbacks) {
    if (callbacks != NULL) memset(callbacks, 0, sizeof(*callbacks));
}

MediaPlayer *player_create(const PlayerConfig *config) {
    gst_init(NULL, NULL);
    MediaPlayer *player = g_new0(MediaPlayer, 1);
    if (player == NULL) return NULL;
    g_mutex_init(&player->mutex);
    if (config != NULL) player->config = *config;
    else player_config_init(&player->config);
    media_info_init(&player->media_info);
    player->state = MEDIA_STATE_NULL;
    player->volume = 1.0;
    player->rate = 1.0;
    return player;
}

void player_destroy(MediaPlayer *player) {
    if (player == NULL) return;
    player_close(player);
    g_mutex_clear(&player->mutex);
    g_free(player);
}

MediaErrorCode player_set_callbacks(MediaPlayer *player,
                                    const PlayerCallbacks *callbacks) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    g_mutex_lock(&player->mutex);
    if (callbacks != NULL) player->callbacks = *callbacks;
    else player_callbacks_init(&player->callbacks);
    g_mutex_unlock(&player->mutex);
    return MEDIA_OK;
}

MediaErrorCode player_open(MediaPlayer *player, const char *uri) {
    if (player == NULL || uri == NULL || uri[0] == '\0') {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    player_close(player);

    gchar *real_uri = strstr(uri, "://") != NULL
        ? g_strdup(uri)
        : gst_filename_to_uri(uri, NULL);
    if (real_uri == NULL) return MEDIA_ERROR_PLAYER_INVALID_URI;

    GstElement *playbin = gst_element_factory_make("playbin", "stage2_playbin");
    if (playbin == NULL) {
        g_free(real_uri);
        return MEDIA_ERROR_PLAYER_CREATE_PIPELINE;
    }

    GstElement *video_sink = gst_element_factory_make(
        player->config.video_sink, "stage2_video_sink");
    GstElement *audio_sink = gst_element_factory_make(
        player->config.audio_sink, "stage2_audio_sink");
    if (video_sink == NULL || audio_sink == NULL) {
        if (video_sink != NULL) gst_object_unref(video_sink);
        if (audio_sink != NULL) gst_object_unref(audio_sink);
        gst_object_unref(playbin);
        g_free(real_uri);
        return MEDIA_ERROR_PLAYER_NO_SINK;
    }

    g_object_set(playbin,
                 "uri", real_uri,
                 "video-sink", video_sink,
                 "audio-sink", audio_sink,
                 "volume", player->volume,
                 "mute", player->mute ? TRUE : FALSE,
                 NULL);
    if (player->config.buffer_duration > 0) {
        g_object_set(playbin,
                     "buffer-duration",
                     (gint64)player->config.buffer_duration * GST_MSECOND,
                     "buffer-size", player->config.buffer_size,
                     NULL);
    }
    if (g_object_class_find_property(
            G_OBJECT_GET_CLASS(video_sink), "sync") != NULL) {
        g_object_set(video_sink, "sync", player->config.sync, NULL);
    }

    player->playbin = playbin;
    player->video_sink = video_sink;
    player->audio_sink = audio_sink;
    player->bus = gst_element_get_bus(playbin);
    gst_bus_set_sync_handler(player->bus, player_bus_sync, player, NULL);

    media_info_init(&player->media_info);
    g_strlcpy(player->media_info.uri, real_uri,
              sizeof(player->media_info.uri));
    discover_media(player, real_uri);
    g_free(real_uri);

    if (gst_element_set_state(playbin, GST_STATE_READY) ==
        GST_STATE_CHANGE_FAILURE) {
        player_close(player);
        return MEDIA_ERROR_PLAYER_STATE_CHANGE;
    }
    g_atomic_int_set(&player->position_thread_running, TRUE);
    player->position_thread = g_thread_new(
        "stage2-player-position", position_thread_main, player);
    return MEDIA_OK;
}

MediaErrorCode player_set_uri(MediaPlayer *player, const char *uri) {
    return player_open(player, uri);
}

MediaErrorCode player_close(MediaPlayer *player) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    if (player->playbin == NULL) return MEDIA_OK;

    g_atomic_int_set(&player->position_thread_running, FALSE);
    if (player->position_thread != NULL) {
        g_thread_join(player->position_thread);
        player->position_thread = NULL;
    }
    gst_bus_set_sync_handler(player->bus, NULL, NULL, NULL);
    gst_element_set_state(player->playbin, GST_STATE_NULL);
    gst_object_unref(player->bus);
    gst_object_unref(player->playbin);
    player->bus = NULL;
    player->playbin = NULL;
    player->video_sink = NULL;
    player->audio_sink = NULL;
    media_info_init(&player->media_info);
    player_notify_state(player, MEDIA_STATE_NULL);
    return MEDIA_OK;
}

static MediaErrorCode set_gst_state(MediaPlayer *player, GstState state) {
    if (player == NULL || player->playbin == NULL) {
        return MEDIA_ERROR_NOT_INITIALIZED;
    }
    return gst_element_set_state(player->playbin, state) ==
                   GST_STATE_CHANGE_FAILURE
        ? MEDIA_ERROR_PLAYER_STATE_CHANGE
        : MEDIA_OK;
}

MediaErrorCode player_play(MediaPlayer *player) {
    return set_gst_state(player, GST_STATE_PLAYING);
}

MediaErrorCode player_pause(MediaPlayer *player) {
    return set_gst_state(player, GST_STATE_PAUSED);
}

MediaErrorCode player_stop(MediaPlayer *player) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    if (player->playbin == NULL) return MEDIA_OK;
    MediaErrorCode result = set_gst_state(player, GST_STATE_READY);
    if (result == MEDIA_OK) player_notify_state(player, MEDIA_STATE_READY);
    return result;
}

MediaErrorCode player_seek(MediaPlayer *player, int64_t position) {
    if (player == NULL || player->playbin == NULL || position < 0) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    gboolean ok = gst_element_seek_simple(
        player->playbin, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, position);
    return ok ? MEDIA_OK : MEDIA_ERROR_IO_SEEK_FAILED;
}

int64_t player_get_position(MediaPlayer *player) {
    if (player == NULL || player->playbin == NULL) return 0;
    gint64 position = 0;
    gst_element_query_position(player->playbin, GST_FORMAT_TIME, &position);
    return position;
}

int64_t player_get_duration(MediaPlayer *player) {
    if (player == NULL) return 0;
    if (player->playbin != NULL) {
        gint64 duration = 0;
        if (gst_element_query_duration(
                player->playbin, GST_FORMAT_TIME, &duration)) {
            return duration;
        }
    }
    return player->media_info.duration;
}

MediaState player_get_state(MediaPlayer *player) {
    if (player == NULL) return MEDIA_STATE_NULL;
    g_mutex_lock(&player->mutex);
    MediaState state = player->state;
    g_mutex_unlock(&player->mutex);
    return state;
}

MediaErrorCode player_get_media_info(MediaPlayer *player, MediaInfo *info) {
    if (player == NULL || info == NULL) return MEDIA_ERROR_INVALID_PARAM;
    g_mutex_lock(&player->mutex);
    *info = player->media_info;
    g_mutex_unlock(&player->mutex);
    info->position = player_get_position(player);
    return MEDIA_OK;
}

MediaErrorCode player_set_volume(MediaPlayer *player, double volume) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    volume = CLAMP(volume, 0.0, 1.0);
    player->volume = volume;
    if (player->playbin != NULL) {
        g_object_set(player->playbin, "volume", volume, NULL);
    }
    return MEDIA_OK;
}

double player_get_volume(MediaPlayer *player) {
    if (player == NULL) return 0.0;
    if (player->playbin != NULL) {
        g_object_get(player->playbin, "volume", &player->volume, NULL);
    }
    return player->volume;
}

MediaErrorCode player_set_mute(MediaPlayer *player, int mute) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    player->mute = mute ? 1 : 0;
    if (player->playbin != NULL) {
        g_object_set(player->playbin, "mute",
                     player->mute ? TRUE : FALSE, NULL);
    }
    return MEDIA_OK;
}

int player_get_mute(MediaPlayer *player) {
    if (player == NULL) return 0;
    if (player->playbin != NULL) {
        gboolean mute = FALSE;
        g_object_get(player->playbin, "mute", &mute, NULL);
        player->mute = mute ? 1 : 0;
    }
    return player->mute;
}

MediaErrorCode player_set_rate(MediaPlayer *player, double rate) {
    if (player == NULL || player->playbin == NULL || rate <= 0.0) {
        return MEDIA_ERROR_INVALID_PARAM;
    }
    gint64 position = player_get_position(player);
    gboolean ok = gst_element_seek(
        player->playbin, rate, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
        GST_SEEK_TYPE_SET, position,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    if (ok) player->rate = rate;
    return ok ? MEDIA_OK : MEDIA_ERROR_PLAYER_STATE_CHANGE;
}

double player_get_rate(MediaPlayer *player) {
    return player != NULL ? player->rate : 1.0;
}

MediaErrorCode player_set_window(MediaPlayer *player, uintptr_t window_id) {
    if (player == NULL) return MEDIA_ERROR_INVALID_PARAM;
    player->window_id = window_id;
    if (player->video_sink != NULL &&
        GST_IS_VIDEO_OVERLAY(player->video_sink)) {
        gst_video_overlay_set_window_handle(
            GST_VIDEO_OVERLAY(player->video_sink), window_id);
    }
    return MEDIA_OK;
}

MediaErrorCode player_set_render_rect(MediaPlayer *player, const Rect *rect) {
    if (player == NULL || rect == NULL || player->video_sink == NULL ||
        !GST_IS_VIDEO_OVERLAY(player->video_sink)) {
        return MEDIA_ERROR_NOT_SUPPORTED;
    }
    gst_video_overlay_set_render_rectangle(
        GST_VIDEO_OVERLAY(player->video_sink),
        rect->x, rect->y, rect->width, rect->height);
    return MEDIA_OK;
}

MediaErrorCode player_get_frame(MediaPlayer *player,
                                uint8_t *buffer,
                                int width,
                                int height) {
    if (player == NULL || player->playbin == NULL || buffer == NULL ||
        width <= 0 || height <= 0) {
        return MEDIA_ERROR_INVALID_PARAM;
    }

    GstCaps *caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        NULL);
    GstSample *sample = NULL;
    g_signal_emit_by_name(player->playbin, "convert-sample", caps, &sample);
    gst_caps_unref(caps);
    if (sample == NULL) return MEDIA_ERROR_NOT_SUPPORTED;

    GstBuffer *gst_buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (gst_buffer == NULL ||
        !gst_buffer_map(gst_buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return MEDIA_ERROR_IO_READ_FAILED;
    }
    gsize required = (gsize)width * (gsize)height * 3U;
    if (map.size < required) {
        gst_buffer_unmap(gst_buffer, &map);
        gst_sample_unref(sample);
        return MEDIA_ERROR_IO_READ_FAILED;
    }
    memcpy(buffer, map.data, required);
    gst_buffer_unmap(gst_buffer, &map);
    gst_sample_unref(sample);
    return MEDIA_OK;
}
