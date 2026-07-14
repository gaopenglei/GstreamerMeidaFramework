#ifndef STAGE1_COMMON_H
#define STAGE1_COMMON_H

#include <gst/gst.h>
#include "learning_stage.h"

#if !MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M1)
#error "Stage 1 examples require MEDIA_FRAMEWORK_STAGE >= 1"
#endif

int stage1_run_pipeline(GstElement *pipeline, gboolean print_messages);
void stage1_print_error(const char *context, const GError *error);

#endif /* STAGE1_COMMON_H */
