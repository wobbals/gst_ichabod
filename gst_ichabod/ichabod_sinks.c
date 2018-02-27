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
  // don't sync on sink. sink should not sync.
  g_object_set(G_OBJECT(sink), "sync", FALSE, NULL);

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

int ichabod_attach_rtp(struct ichabod_bin_s* bin,
                       unsigned long audio_ssrc, int audio_port,
                       const char* audio_host,
                       char audio_pt,
                       unsigned long video_ssrc, int video_port,
                       const char* video_host,
                       char video_pt)
{
  GstElement* opus_enc = gst_element_factory_make("opusenc", "opusenc");
  GstElement* opus_pay = gst_element_factory_make("rtpopuspay", "artp");
  GstElement* opus_sink = gst_element_factory_make("udpsink", "anetsink");

  GstElement* h264_pay = gst_element_factory_make("rtph264pay", "vrtp");
  GstElement* h264_sink = gst_element_factory_make("udpsink", "vnetsink");

  g_object_set(G_OBJECT(opus_pay), "pt", audio_pt, NULL);
  g_object_set(G_OBJECT(opus_pay), "ssrc", audio_ssrc, NULL);
  g_object_set(G_OBJECT(opus_sink), "host", audio_host, NULL);
  g_object_set(G_OBJECT(opus_sink), "port", audio_port, NULL);
  g_object_set(G_OBJECT(h264_pay), "pt", video_pt, NULL);
  g_object_set(G_OBJECT(h264_pay), "ssrc", video_ssrc, NULL);
  g_object_set(G_OBJECT(h264_sink), "host", video_host, NULL);
  g_object_set(G_OBJECT(h264_sink), "port", video_port, NULL);

  int ret = ichabod_bin_add_element(bin, opus_enc);
  ret = ichabod_bin_add_element(bin, opus_pay);
  ret = ichabod_bin_add_element(bin, opus_sink);
  ret = ichabod_bin_add_element(bin, h264_pay);
  ret = ichabod_bin_add_element(bin, h264_sink);

  gboolean result = gst_element_link_many(opus_enc, opus_pay, opus_sink, NULL);
  result = gst_element_link_many(h264_pay, h264_sink, NULL);

  GstPad* apad = gst_element_get_static_pad(opus_enc, "sink");
  GstPad* vpad = gst_element_get_static_pad(h264_pay, "sink");

  ret = ichabod_bin_attach_raw_audio_sink(bin, apad);
  ret = ichabod_bin_attach_enc_video_sink(bin, vpad);
  return ret;
}
