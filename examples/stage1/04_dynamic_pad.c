#include "stage1_common.h"

#include <stdio.h>

typedef struct {
    GstElement *convert;
    gint linked;
} DynamicPadContext;

static void on_pad_added(GstElement *decodebin, GstPad *new_pad, gpointer data) {
    (void)decodebin;
    DynamicPadContext *context = (DynamicPadContext *)data;
    if (g_atomic_int_get(&context->linked)) {
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(new_pad);
    if (caps == NULL) {
        caps = gst_pad_query_caps(new_pad, NULL);
    }
    if (caps == NULL || gst_caps_is_empty(caps)) {
        if (caps != NULL) {
            gst_caps_unref(caps);
        }
        return;
    }

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *media_type = gst_structure_get_name(structure);
    printf("decodebin added pad with caps: %s\n", media_type);

    if (g_str_has_prefix(media_type, "audio/x-raw")) {
        GstPad *sink_pad = gst_element_get_static_pad(context->convert, "sink");
        if (gst_pad_link(new_pad, sink_pad) == GST_PAD_LINK_OK) {
            g_atomic_int_set(&context->linked, TRUE);
            printf("Dynamic audio pad linked\n");
        } else {
            fprintf(stderr, "Could not link dynamic audio pad\n");
        }
        gst_object_unref(sink_pad);
    }

    gst_caps_unref(caps);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_pipeline_new("dynamic-pad-pipeline");
    GstElement *source = gst_element_factory_make("audiotestsrc", "source");
    GstElement *encoder = gst_element_factory_make("wavenc", "encoder");
    GstElement *decoder = gst_element_factory_make("decodebin", "decoder");
    GstElement *convert = gst_element_factory_make("audioconvert", "convert");
    GstElement *resample = gst_element_factory_make("audioresample", "resample");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");
    if (pipeline == NULL || source == NULL || encoder == NULL ||
        decoder == NULL || convert == NULL || resample == NULL || sink == NULL) {
        fprintf(stderr, "Could not create dynamic-pad lesson elements\n");
        if (pipeline != NULL) {
            gst_object_unref(pipeline);
        }
        return 1;
    }

    g_object_set(source, "num-buffers", 32, NULL);
    g_object_set(sink, "sync", FALSE, NULL);
    gst_bin_add_many(GST_BIN(pipeline), source, encoder, decoder,
                     convert, resample, sink, NULL);

    if (!gst_element_link_many(source, encoder, decoder, NULL) ||
        !gst_element_link_many(convert, resample, sink, NULL)) {
        fprintf(stderr, "Could not link static portions of pipeline\n");
        gst_object_unref(pipeline);
        return 1;
    }

    DynamicPadContext context = {
        .convert = convert,
        .linked = FALSE
    };
    g_signal_connect(decoder, "pad-added", G_CALLBACK(on_pad_added), &context);

    int result = stage1_run_pipeline(pipeline, FALSE);
    if (!g_atomic_int_get(&context.linked)) {
        fprintf(stderr, "decodebin did not expose a usable audio pad\n");
        result = 1;
    }

    gst_object_unref(pipeline);
    return result;
}
