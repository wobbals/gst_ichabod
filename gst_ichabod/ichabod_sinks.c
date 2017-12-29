//
//  ichabod_sinks.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//

#include <assert.h>
#include <gst/gst.h>
#include "ichabod_sinks.h"

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, const char* broadcast_url) {
  GstElement* mux = gst_element_factory_make("flvmux", "flvmux");
  GstElement* sink = gst_element_factory_make("rtmpsink", "rtmpsink");

  g_object_set(G_OBJECT(mux), "streamable", TRUE, NULL);
  g_object_set(G_OBJECT(sink), "location", broadcast_url, NULL);

  int ret = ichabod_bin_add_element(bin, mux);
  ret = ichabod_bin_add_element(bin, sink);
  gboolean result = gst_element_link(mux, sink);

  GstPad* v_mux_sink = gst_element_get_request_pad(mux, "video");
  GstPad* a_mux_sink = gst_element_get_request_pad(mux, "audio");

  return ichabod_bin_attach_mux_sink_pad(bin, a_mux_sink, v_mux_sink);
}

int ichabod_attach_file(struct ichabod_bin_s* bin, const char* path) {
  GstElement* mux = gst_element_factory_make("mp4mux", "mymux");
  GstElement* sink = gst_element_factory_make("filesink", "fsink");

  // configure multiplexer
  //g_signal_connect (mux, "pad-added",
  //                  G_CALLBACK (pad_added_handler), &ichabod);
  g_object_set(G_OBJECT(mux), "faststart", TRUE, NULL);
  //g_object_set(G_OBJECT(mux), "streamable", TRUE, NULL);

  // configure output sink
  g_object_set (G_OBJECT (sink), "location", path, NULL);
  //g_object_set (G_OBJECT (sink), "async", FALSE, NULL);

  int ret = ichabod_bin_add_element(bin, mux);
  ret = ichabod_bin_add_element(bin, sink);
  GstPad* apad = gst_element_get_request_pad(mux, "audio_%u");
  GstPad* vpad = gst_element_get_request_pad(mux, "video_%u");
  assert(!ichabod_bin_attach_mux_sink_pad(bin, apad, vpad));
  gboolean result = gst_element_link(mux, sink);
  return !result;
}
