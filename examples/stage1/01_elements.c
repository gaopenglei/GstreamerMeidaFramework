#include "stage1_common.h"

#include <stdio.h>

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_pipeline_new("element-lesson");
    GstElement *source = gst_element_factory_make("videotestsrc", "source");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");
    if (pipeline == NULL || source == NULL || sink == NULL) {
        fprintf(stderr, "Could not create required GStreamer elements\n");
        if (pipeline != NULL) {
            gst_object_unref(pipeline);
        }
        if (source != NULL) {
            gst_object_unref(source);
        }
        if (sink != NULL) {
            gst_object_unref(sink);
        }
        return 1;
    }

    GstElementFactory *factory = gst_element_get_factory(source);
    printf("Element: %s\n", GST_ELEMENT_NAME(source));
    printf("Factory: %s\n", gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
    printf("Class: %s\n",
           gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS));

    g_object_set(source, "num-buffers", 1, NULL);
    g_object_set(sink, "sync", FALSE, NULL);
    gst_bin_add_many(GST_BIN(pipeline), source, sink, NULL);
    if (!gst_element_link(source, sink)) {
        fprintf(stderr, "Could not link source to sink\n");
        gst_object_unref(pipeline);
        return 1;
    }

    int result = stage1_run_pipeline(pipeline, FALSE);
    gst_object_unref(pipeline);
    return result;
}
