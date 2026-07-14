#include "stage1_common.h"

#include <stdio.h>

static GstPad *request_tee_pad(GstElement *tee) {
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_element_request_pad_simple(tee, "src_%u");
#else
    return gst_element_get_request_pad(tee, "src_%u");
#endif
}

static gboolean link_tee_branch(GstElement *tee,
                                GstElement *queue,
                                GstPad **requested_pad) {
    GstPad *tee_pad = request_tee_pad(tee);
    GstPad *queue_pad = gst_element_get_static_pad(queue, "sink");
    if (tee_pad == NULL || queue_pad == NULL) {
        if (tee_pad != NULL) {
            gst_object_unref(tee_pad);
        }
        if (queue_pad != NULL) {
            gst_object_unref(queue_pad);
        }
        return FALSE;
    }

    GstPadLinkReturn result = gst_pad_link(tee_pad, queue_pad);
    gst_object_unref(queue_pad);
    if (result != GST_PAD_LINK_OK) {
        gst_element_release_request_pad(tee, tee_pad);
        gst_object_unref(tee_pad);
        return FALSE;
    }

    printf("Requested and linked pad: %s\n", GST_PAD_NAME(tee_pad));
    *requested_pad = tee_pad;
    return TRUE;
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    GstElement *pipeline = gst_pipeline_new("request-pad-pipeline");
    GstElement *source = gst_element_factory_make("audiotestsrc", "source");
    GstElement *tee = gst_element_factory_make("tee", "tee");
    GstElement *queue1 = gst_element_factory_make("queue", "queue1");
    GstElement *queue2 = gst_element_factory_make("queue", "queue2");
    GstElement *sink1 = gst_element_factory_make("fakesink", "sink1");
    GstElement *sink2 = gst_element_factory_make("fakesink", "sink2");
    if (pipeline == NULL || source == NULL || tee == NULL || queue1 == NULL ||
        queue2 == NULL || sink1 == NULL || sink2 == NULL) {
        fprintf(stderr, "Could not create request-pad lesson elements\n");
        if (pipeline != NULL) {
            gst_object_unref(pipeline);
        }
        return 1;
    }

    g_object_set(source, "num-buffers", 20, NULL);
    g_object_set(sink1, "sync", FALSE, NULL);
    g_object_set(sink2, "sync", FALSE, NULL);
    gst_bin_add_many(GST_BIN(pipeline), source, tee, queue1, queue2,
                     sink1, sink2, NULL);

    if (!gst_element_link(source, tee) ||
        !gst_element_link(queue1, sink1) ||
        !gst_element_link(queue2, sink2)) {
        fprintf(stderr, "Could not link static request-pad branches\n");
        gst_object_unref(pipeline);
        return 1;
    }

    GstPad *tee_pad1 = NULL;
    GstPad *tee_pad2 = NULL;
    if (!link_tee_branch(tee, queue1, &tee_pad1) ||
        !link_tee_branch(tee, queue2, &tee_pad2)) {
        fprintf(stderr, "Could not request and link tee pads\n");
        if (tee_pad1 != NULL) {
            gst_element_release_request_pad(tee, tee_pad1);
            gst_object_unref(tee_pad1);
        }
        gst_object_unref(pipeline);
        return 1;
    }

    int result = stage1_run_pipeline(pipeline, FALSE);

    gst_element_release_request_pad(tee, tee_pad1);
    gst_element_release_request_pad(tee, tee_pad2);
    gst_object_unref(tee_pad1);
    gst_object_unref(tee_pad2);
    gst_object_unref(pipeline);
    return result;
}
