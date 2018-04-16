//
//  ichabod_sinks.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <gst/gst.h>
#include "ichabod_sinks.h"

struct sink_container {
  char *factory_name;
  gboolean sync;
  struct ichabod_bin_s* bin;
  GstElement* mux;
};

void make_sink(const char *location, struct sink_container* container) {
  printf("\n location: %s \n", location);
  // Location is also an unique name of sink
  GstElement* sink = gst_element_factory_make(container->factory_name, location);
  g_object_set(G_OBJECT(sink), "location", location, NULL);
  g_object_set(G_OBJECT(sink), "sync", container->sync, NULL);
  int ret = ichabod_bin_add_element(container->bin, sink);
  gboolean result = gst_element_link(container->mux, sink);
}

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, GSList *broadcast_urls) {

  // Prepare mux element
  GstElement* mux = gst_element_factory_make("flvmux", "flvmux");
  g_object_set(G_OBJECT(mux), "streamable", TRUE, NULL);
  int ret = ichabod_bin_add_element(bin, mux);

  // Prepare rmpt container, which needs to be passed into sink loop function
  struct sink_container rtmp_container;
  rtmp_container.mux = mux;
  rtmp_container.bin = bin;
  rtmp_container.factory_name = "rtmpsink";
  // don't sync on sink. sink should not sync.
  rtmp_container.sync = FALSE;

  // Go through each element of broadcast list to append bin struct
  g_slist_foreach(broadcast_urls, (GFunc)make_sink, &rtmp_container);

  GstPad* v_mux_sink = gst_element_get_request_pad(mux, "video");
  GstPad* a_mux_sink = gst_element_get_request_pad(mux, "audio");

  return ichabod_bin_attach_mux_sink_pad(bin, a_mux_sink, v_mux_sink);
  // GstElement* mux = gst_element_factory_make("flvmux", "flvmux");
  // GstElement* sink = gst_element_factory_make("rtmpsink", "rtmpsink");

  // g_object_set(G_OBJECT(mux), "streamable", TRUE, NULL);
  // g_object_set(G_OBJECT(sink), "location", "rtmp://a.rtmp.youtube.com/live2/k8v2-5stf-utk5-cuhc", NULL);
  // // don't sync on sink. sink should not sync.
  // g_object_set(G_OBJECT(sink), "sync", FALSE, NULL);

  // int ret = ichabod_bin_add_element(bin, mux);
  // ret = ichabod_bin_add_element(bin, sink);
  // // ret = ichabod_bin_add_element(bin, sink2);
  // gboolean result = gst_element_link(mux, sink);
  // // gboolean result2 = gst_element_link(mux, sink2);

  // GstPad* v_mux_sink = gst_element_get_request_pad(mux, "video");
  // GstPad* a_mux_sink = gst_element_get_request_pad(mux, "audio");

  // return ichabod_bin_attach_mux_sink_pad(bin, a_mux_sink, v_mux_sink);
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
