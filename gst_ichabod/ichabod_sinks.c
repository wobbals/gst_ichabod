//
//  ichabod_sinks.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//

#include <assert.h>
#include <gst/gst.h>
#include <gst/rtp/rtp.h>
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
  g_object_set(G_OBJECT(sink), "location", path, NULL);
  g_object_set(G_OBJECT(sink), "async", FALSE, NULL);

  int ret = ichabod_bin_add_element(bin, mux);
  ret = ichabod_bin_add_element(bin, sink);
  GstPad* apad = gst_element_get_request_pad(mux, "audio_%u");
  GstPad* vpad = gst_element_get_request_pad(mux, "video_%u");
  assert(!ichabod_bin_attach_mux_sink_pad(bin, apad, vpad));
  gboolean result = gst_element_link(mux, sink);
  return !result;
}

int ichabod_attach_rtp(struct ichabod_bin_s* bin,
                       struct rtp_opts_s* rtp_opts)
{
  GstElement* rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
  g_object_set(G_OBJECT(rtpbin), "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);
  int ret = ichabod_bin_add_element(bin, rtpbin);

  GstPad* rtp_sink_v = gst_element_get_request_pad(rtpbin, "send_rtp_sink_%u");
  // is it safe to access like this? check the plugins-good examples
  GstPad* rtp_src_v = gst_element_get_static_pad(rtpbin, "send_rtp_src_0");

  // TODO add rtcpMux & rtxbuffer
  if (rtp_opts->video_rtcp_port) {
    GstPad* rtcp_src_v = gst_element_get_request_pad(rtpbin, "send_rtcp_src_0");
    GstElement* rtcp_sink = gst_element_factory_make("udpsink", "vrtcpsink");
    ichabod_bin_add_element(bin, rtcp_sink);
    g_object_set(G_OBJECT(rtcp_sink), "host", rtp_opts->audio_host, NULL);
    g_object_set(G_OBJECT(rtcp_sink), "port", rtp_opts->audio_rtcp_port, NULL);
    g_object_set(G_OBJECT(rtcp_sink), "sync", FALSE, NULL);
    GstPad* rtcp_sink_pad = gst_element_get_static_pad(rtcp_sink, "sink");
    GstPadLinkReturn link = gst_pad_link(rtcp_src_v, rtcp_sink_pad);
    if (GST_PAD_LINK_OK == ret) {
      printf("added video rtcp sender on port %d\n", rtp_opts->video_rtcp_port);
    } else {
      printf("failed to link rtcp sender: %d\n", link);
    }
  }

  GstPad* rtp_sink_a = gst_element_get_request_pad(rtpbin, "send_rtp_sink_%u");
  GstPad* rtp_src_a = gst_element_get_static_pad(rtpbin, "send_rtp_src_1");

  if (rtp_opts->audio_rtcp_port) {
    GstPad* rtcp_src_a = gst_element_get_request_pad(rtpbin, "send_rtcp_src_1");
    GstElement* rtcp_sink = gst_element_factory_make("udpsink", "artcpsink");
    ichabod_bin_add_element(bin, rtcp_sink);
    g_object_set(G_OBJECT(rtcp_sink), "host", rtp_opts->audio_host, NULL);
    g_object_set(G_OBJECT(rtcp_sink), "port", rtp_opts->audio_rtcp_port, NULL);
    g_object_set(G_OBJECT(rtcp_sink), "sync", FALSE, NULL);
    GstPad* rtcp_sink_pad = gst_element_get_static_pad(rtcp_sink, "sink");
    GstPadLinkReturn link = gst_pad_link(rtcp_src_a, rtcp_sink_pad);
    if (GST_PAD_LINK_OK == link) {
      printf("added audio rtcp sender on port %d\n", rtp_opts->audio_rtcp_port);
    } else {
      printf("failed to link rtcp sender: %d\n", link);
    }
  }

  GstElement* opus_enc = gst_element_factory_make("opusenc", "opusenc");
  GstElement* opus_pay = gst_element_factory_make("rtpopuspay", "artp");
  GstElement* opus_sink = gst_element_factory_make("udpsink", "artpsink");
  GstElement* h264_pay = gst_element_factory_make("rtph264pay", "vrtp");
  GstElement* h264_sink = gst_element_factory_make("udpsink", "vrtpsink");

  ret = ichabod_bin_add_element(bin, opus_enc);
  ret = ichabod_bin_add_element(bin, opus_pay);
  ret = ichabod_bin_add_element(bin, opus_sink);
  ret = ichabod_bin_add_element(bin, h264_pay);
  ret = ichabod_bin_add_element(bin, h264_sink);

  g_object_set(G_OBJECT(opus_pay), "pt", rtp_opts->audio_pt, NULL);
  g_object_set(G_OBJECT(opus_pay), "ssrc", rtp_opts->audio_ssrc, NULL);

  g_object_set(G_OBJECT(opus_sink), "host", rtp_opts->audio_host, NULL);
  g_object_set(G_OBJECT(opus_sink), "port", rtp_opts->audio_port, NULL);
  g_object_set(G_OBJECT(opus_sink), "sync", FALSE, NULL);

  g_object_set(G_OBJECT(h264_pay), "pt", rtp_opts->video_pt, NULL);
  g_object_set(G_OBJECT(h264_pay), "config-interval", 2, NULL);
  g_object_set(G_OBJECT(h264_pay), "ssrc", rtp_opts->video_ssrc, NULL);

  g_object_set(G_OBJECT(h264_sink), "host", rtp_opts->video_host, NULL);
  g_object_set(G_OBJECT(h264_sink), "port", rtp_opts->video_port, NULL);
  g_object_set(G_OBJECT(h264_sink), "sync", FALSE, NULL);


  gboolean result = gst_element_link(opus_enc, opus_pay);

  GstPad* opus_sink_pad = gst_element_get_static_pad(opus_sink, "sink");
  GstPadLinkReturn status = gst_pad_link(rtp_src_a, opus_sink_pad);

  GstPad* opus_pay_src = gst_element_get_static_pad(opus_pay, "src");
  status = gst_pad_link(opus_pay_src, rtp_sink_a);

  GstPad* h264_sink_pad = gst_element_get_static_pad(h264_sink, "sink");
  status = gst_pad_link(rtp_src_v, h264_sink_pad);

  GstPad* h264_pay_src = gst_element_get_static_pad(h264_pay, "src");
  status = gst_pad_link(h264_pay_src, rtp_sink_v);

  GstPad* apad = gst_element_get_static_pad(opus_enc, "sink");
  GstPad* vpad = gst_element_get_static_pad(h264_pay, "sink");

  ret = ichabod_bin_attach_raw_audio_sink(bin, apad);
  ret = ichabod_bin_attach_enc_video_sink(bin, vpad);
  return ret;
}
