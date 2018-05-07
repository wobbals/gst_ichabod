//
//  screencast_src.c
//  gst_ichabod
//
//  Created by Charley Robinson on 4/13/18.
//

#include <stdlib.h>
#include <gst/app/gstappsrc.h>
#include <uv.h>
#include "screencast_src.h"
#include "wallclock.h"
#include "base64.h"

struct screencast_src_s {
  GstElement* element;
  GstClock* wall_clock;

  uv_mutex_t lock;
  char allow_data;
};

static void app_src_enough_data(GstAppSrc *src, gpointer p);
static void app_src_need_data(GstAppSrc *src, guint length, gpointer p);
static void app_src_seek_data(GstAppSrc *src, guint64 offset, gpointer p);

void screencast_src_alloc(struct screencast_src_s** screencast_src_out) {
  struct screencast_src_s* pthis = (struct screencast_src_s*)
  calloc(1, sizeof(struct screencast_src_s));

  uv_mutex_init(&pthis->lock);

  pthis->element = gst_element_factory_make("appsrc", NULL);
  GstCaps* caps = gst_caps_new_simple("image/jpeg", NULL);
  gst_app_src_set_caps(pthis->element, caps);
  gst_app_src_set_size(pthis->element, -1);
  gst_app_src_set_duration(pthis->element, GST_CLOCK_TIME_NONE);
  gst_app_src_set_stream_type(pthis->element, GST_APP_STREAM_TYPE_STREAM);
  //gst_base_src_set_format(GST_BASE_SRC(pthis->element), GST_FORMAT_TIME);

  static GstAppSrcCallbacks callbacks = { 0 };
  callbacks.enough_data = app_src_enough_data;
  callbacks.need_data = app_src_need_data;
  callbacks.seek_data = app_src_seek_data;
  gst_app_src_set_callbacks(pthis->element, &callbacks, pthis, NULL);

  pthis->wall_clock = gst_wall_clock_new();

  *screencast_src_out = pthis;
}

void screencast_src_free(struct screencast_src_s* pthis) {
  gst_object_unref(pthis->element);
  pthis->element = NULL;
  gst_object_unref(pthis->wall_clock);
  pthis->wall_clock = NULL;
  uv_mutex_destroy(&pthis->lock);
  free(pthis);
}

void screencast_src_push_frame(struct screencast_src_s* pthis,
                               uint64_t timestamp, const char* frame_base64)
{
  uv_mutex_lock(&pthis->lock);
  char allow_frame = pthis->allow_data;
  uv_mutex_unlock(&pthis->lock);
  if (!allow_frame) {
    g_print("screencastsrc: skipping incoming frame (not ready)\n");
    return;
  }

  GstClock* master_clock = gst_element_get_clock(pthis->element);
  if (!master_clock) {
    g_print("screencastsrc: skip frame: no master clock to sync to\n");
    return;
  }

  uv_mutex_lock(&pthis->lock);
  if (master_clock != gst_clock_get_master(pthis->wall_clock)) {
    g_print("screencastsrc: new master clock detected.\n");
    gst_clock_set_master(pthis->wall_clock, master_clock);
    // one-shot bootleg calibration to get timestamps adjusted immediately
    GstClockTime internal = gst_clock_get_internal_time(pthis->wall_clock);
    GstClockTime external = gst_clock_get_time(master_clock);
    // just assume the clocks run at the same speed for now (should be close)
    gst_clock_set_calibration(pthis->wall_clock, internal, external, 1, 1);
  }
  uv_mutex_unlock(&pthis->lock);

  // base64 decode
  size_t b_length = 0;
  const uint8_t* b_img =
  base64_decode((const unsigned char*)frame_base64,
                strlen(frame_base64),
                &b_length);
  // create buffer
  GstBuffer* buf = gst_buffer_new_wrapped((gpointer)b_img, b_length);

  // input timestamp is in millis; convert before adjusting to GstClock
  timestamp *= GST_MSECOND;

  GstClockTime ts = gst_wall_clock_adjust_safe(pthis->wall_clock, timestamp);
  buf->dts = ts;
  buf->pts = ts;
  g_print("screencastsrc: push pts %ld\n", buf->pts);

  // gst_app_src_push_buffer
  gst_app_src_push_buffer(pthis->element, buf);
}

void screencast_src_send_eos(struct screencast_src_s* pthis) {
  g_print("screencastsrc: received EOS\n");
  gst_app_src_end_of_stream(pthis->element);
}

GstElement* screencast_src_get_element(struct screencast_src_s* pthis) {
  return pthis->element;
}

static void app_src_enough_data(GstAppSrc *src, gpointer p) {
  struct screencast_src_s* pthis = (struct screencast_src_s*)p;
  g_print("screencastsrc: enough_data\n");
  uv_mutex_lock(&pthis->lock);
  pthis->allow_data = 0;
  uv_mutex_unlock(&pthis->lock);
}

static void app_src_need_data(GstAppSrc *src, guint length, gpointer p) {
  struct screencast_src_s* pthis = (struct screencast_src_s*)p;
  g_print("screencastsrc: need_data\n");
  uv_mutex_lock(&pthis->lock);
  pthis->allow_data = 1;
  uv_mutex_unlock(&pthis->lock);
}

static void app_src_seek_data(GstAppSrc *src, guint64 offset, gpointer p) {
  g_print("screencastsrc: seek_data\n");
}


