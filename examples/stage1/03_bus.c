#include "stage1_common.h"

#include <stdio.h>

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(
        "audiotestsrc num-buffers=20 ! audioconvert ! fakesink sync=false",
        &error);
    if (pipeline == NULL) {
        stage1_print_error("Could not parse pipeline", error);
        g_clear_error(&error);
        return 1;
    }
    if (error != NULL) {
        stage1_print_error("Pipeline was only partially constructed", error);
        g_clear_error(&error);
        gst_object_unref(pipeline);
        return 1;
    }

    printf("Watching ERROR, EOS and STATE_CHANGED messages\n");
    int result = stage1_run_pipeline(pipeline, TRUE);
    gst_object_unref(pipeline);
    return result;
}
