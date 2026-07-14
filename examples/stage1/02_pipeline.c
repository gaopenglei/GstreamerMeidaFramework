#include "stage1_common.h"

#include <stdio.h>

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_pipeline_new("manual-pipeline");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");
    if (pipeline == NULL || source == NULL || convert == NULL || sink == NULL) {
        fprintf(stderr, "Could not create the manual pipeline elements\n");
        if (pipeline != NULL) {
            gst_object_unref(pipeline);
        }
        return 1;
    }

    g_object_set(source, "num-buffers", 30, "pattern", 0, NULL);
    g_object_set(sink, "sync", FALSE, NULL);
    gst_bin_add_many(GST_BIN(pipeline), source, convert, sink, NULL);

    if (!gst_element_link_many(source, convert, sink, NULL)) {
        fprintf(stderr, "Could not link the manual pipeline\n");
        gst_object_unref(pipeline);
        return 1;
    }

    printf("Pipeline: videotestsrc -> videoconvert -> fakesink\n");
    int result = stage1_run_pipeline(pipeline, FALSE);
    gst_object_unref(pipeline);
    return result;
}
