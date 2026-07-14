#include "stage1_common.h"

#include <stdio.h>

void stage1_print_error(const char *context, const GError *error) {
    fprintf(stderr, "%s: %s\n", context,
            error != NULL ? error->message : "unknown error");
}

int stage1_run_pipeline(GstElement *pipeline, gboolean print_messages) {
    if (pipeline == NULL) {
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    GstStateChangeReturn state_ret =
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (state_ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "Failed to set pipeline to PLAYING\n");
        gst_object_unref(bus);
        return 1;
    }

    int result = 0;
    gboolean finished = FALSE;
    while (!finished) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus,
            GST_CLOCK_TIME_NONE,
            GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED);

        if (message == NULL) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ERROR: {
                GError *error = NULL;
                gchar *debug = NULL;
                gst_message_parse_error(message, &error, &debug);
                stage1_print_error("Pipeline error", error);
                if (debug != NULL) {
                    fprintf(stderr, "Debug details: %s\n", debug);
                }
                g_clear_error(&error);
                g_free(debug);
                result = 1;
                finished = TRUE;
                break;
            }
            case GST_MESSAGE_EOS:
                if (print_messages) {
                    printf("Bus: EOS\n");
                }
                finished = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                if (print_messages &&
                    GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
                    GstState old_state;
                    GstState new_state;
                    GstState pending_state;
                    gst_message_parse_state_changed(
                        message, &old_state, &new_state, &pending_state);
                    printf("Bus: pipeline %s -> %s (pending: %s)\n",
                           gst_element_state_get_name(old_state),
                           gst_element_state_get_name(new_state),
                           gst_element_state_get_name(pending_state));
                }
                break;
            default:
                break;
        }

        gst_message_unref(message);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    return result;
}
