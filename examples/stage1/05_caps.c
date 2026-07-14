#include "stage1_common.h"

#include <stdio.h>

static GstPadProbeReturn on_caps_event(GstPad *pad,
                                       GstPadProbeInfo *info,
                                       gpointer data) {
    (void)pad;
    gint *observed = (gint *)data;
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
        GstCaps *caps = NULL;
        gst_event_parse_caps(event, &caps);
        gchar *caps_text = gst_caps_to_string(caps);
        printf("Negotiated caps: %s\n", caps_text);
        g_free(caps_text);
        g_atomic_int_set(observed, TRUE);
    }
    return GST_PAD_PROBE_OK;
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_pipeline_new("caps-pipeline");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *filter = gst_element_factory_make("capsfilter", "filter");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");
    if (pipeline == NULL || source == NULL || filter == NULL || sink == NULL) {
        fprintf(stderr, "Could not create caps lesson elements\n");
        if (pipeline != NULL) {
            gst_object_unref(pipeline);
        }
        return 1;
    }

    GstCaps *caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);
    g_object_set(source, "num-buffers", 10, NULL);
    g_object_set(filter, "caps", caps, NULL);
    g_object_set(sink, "sync", FALSE, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), source, filter, sink, NULL);
    if (!gst_element_link_many(source, filter, sink, NULL)) {
        fprintf(stderr, "Could not link caps pipeline\n");
        gst_object_unref(pipeline);
        return 1;
    }

    gint observed = FALSE;
    GstPad *src_pad = gst_element_get_static_pad(filter, "src");
    gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                      on_caps_event, &observed, NULL);
    gst_object_unref(src_pad);

    int result = stage1_run_pipeline(pipeline, FALSE);
    if (!g_atomic_int_get(&observed)) {
        fprintf(stderr, "No CAPS event was observed\n");
        result = 1;
    }

    gst_object_unref(pipeline);
    return result;
}
