//
//  broadcast_sink.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//  Copyright © 2017 Charley Robinson. All rights reserved.
//

#include "broadcast_sink.h"
#include <gst/gst.h>

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
