//
//  rtp_recv.c
//  gst_ichabod
//
//  Created by Charley Robinson on 4/5/18.
//

#include "rtp_relay.h"

struct rtp_relay_s {
  struct rtp_relay_config_s config;
  GstBin* bin;
  GstElement* rtpbin;

  GstPad* video_send_rtp_sink;
  GstPad* audio_send_rtp_sink;
};

static void on_rtpbin_pad_added(GstElement * element, GstPad * new_pad,
                                gpointer data);

void rtp_relay_alloc(struct rtp_relay_s** rtp_recv_out) {
  struct rtp_relay_s* pthis = (struct rtp_relay_s*)
  calloc(1, sizeof(struct rtp_relay_s));

  pthis->bin = GST_BIN(gst_bin_new("rtp_relay"));
  pthis->rtpbin = gst_element_factory_make("rtpbin", NULL);
  gboolean ret = gst_bin_add(pthis->bin, pthis->rtpbin);
  g_assert(ret);

  g_signal_connect(pthis->rtpbin, "pad-added",
                   G_CALLBACK(on_rtpbin_pad_added), pthis);

  *rtp_recv_out = pthis;
}

void rtp_relay_free(struct rtp_relay_s* pthis) {
  gst_object_unref(pthis->bin);
  pthis->bin = NULL;
  free(pthis);
}

static void on_rtpbin_pad_added(GstElement* element, GstPad* new_pad,
                                gpointer data)
{
  struct rtp_relay_s* pthis = data;
  g_assert(element == pthis->rtpbin);
  gchar* name = gst_pad_get_name(new_pad);
  g_print("rtp_relay: rtpbin pad added: %s\n", name);
}

static GstElement* create_udp_src(int port) {
  GstElement* src = gst_element_factory_make("udpsrc", NULL);
  g_object_set(G_OBJECT(src), "port", port, NULL);
  return src;
}

static GstPad* create_h264_recv_chain(struct rtp_relay_s* pthis) {
  GstElement* depacketizer = gst_element_factory_make("rtph264depay", NULL);
  GstElement* decoder = gst_element_factory_make("avdec_h264", NULL);
  gst_bin_add_many(pthis->bin, depacketizer, decoder, NULL);
  return NULL;
}

int rtp_relay_config(struct rtp_relay_s* pthis,
                    struct rtp_relay_config_s* config)
{
  GstPadLinkReturn pad_link;
  memcpy(&pthis->config, config, sizeof(struct rtp_relay_config_s));

  if (pthis->config.recv_enabled) {
    GstPad* rtp_video_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtp_sink_0");
    GstElement* udp_video_recv = create_udp_src(config->video_recv_rtp_port);
    gst_bin_add(pthis->bin, udp_video_recv);
    GstPad* udp_video_src = gst_element_get_static_pad(udp_video_recv, "src");
    pad_link = gst_pad_link(udp_video_src, rtp_video_sink);
    g_assert(!pad_link);

    GstPad* rtp_audio_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtp_sink_1");
    GstElement* udp_audio_recv = create_udp_src(config->audio_recv_rtp_port);
    gst_bin_add(pthis->bin, udp_audio_recv);
    GstPad* udp_audio_src = gst_element_get_static_pad(udp_audio_recv, "src");
    pad_link = gst_pad_link(udp_audio_src, rtp_audio_sink);
    g_assert(!pad_link);
  }

  if (pthis->config.send_enabled){
    pthis->video_send_rtp_sink =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtp_sink_0");
    GstPad* video_send_rtp_src =
    gst_element_get_static_pad(pthis->rtpbin, "send_rtp_src_0");
    GstPad* video_send_rtcp_src =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtcp_src_0");

    GstElement* udp_video_rtp_send = gst_element_factory_make("udpsink", NULL);
    gst_bin_add(pthis->bin, udp_video_rtp_send);
    g_object_set(G_OBJECT(udp_video_rtp_send), "sync", FALSE, NULL);
    g_object_set(G_OBJECT(udp_video_rtp_send), "host",
                 pthis->config.video_send_rtp_host, NULL);
    g_object_set(G_OBJECT(udp_video_rtp_send),
                 "port", pthis->config.video_send_rtp_port, NULL);
    g_object_set(G_OBJECT(udp_video_rtp_send),
                 "bind-port", pthis->config.video_recv_rtp_port, NULL);

    GstPad* udp_video_rtp_send_sink =
    gst_element_get_static_pad(udp_video_rtp_send, "sink");
    pad_link = gst_pad_link(video_send_rtp_src, udp_video_rtp_send_sink);
    g_assert(!pad_link);

    GstElement* udp_video_rtcp_send = gst_element_factory_make("udpsink", NULL);
    gst_bin_add(pthis->bin, udp_video_rtcp_send);
    g_object_set(G_OBJECT(udp_video_rtcp_send), "sync", FALSE, NULL);
    g_object_set(G_OBJECT(udp_video_rtcp_send), "host",
                 pthis->config.video_send_rtp_host, NULL);
    g_object_set(G_OBJECT(udp_video_rtcp_send),
                 "port", pthis->config.video_send_rtcp_port, NULL);
    g_object_set(G_OBJECT(udp_video_rtcp_send),
                 "bind-port", pthis->config.video_recv_rtcp_port, NULL);

    GstPad* udp_video_rtcp_send_sink =
    gst_element_get_static_pad(udp_video_rtcp_send, "sink");
    pad_link = gst_pad_link(video_send_rtcp_src, udp_video_rtcp_send_sink);
    g_assert(!pad_link);

    pthis->audio_send_rtp_sink =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtp_sink_1");
    GstPad* audio_send_rtp_src =
    gst_element_get_static_pad(pthis->rtpbin, "send_rtp_src_1");
    GstPad* audio_send_rtcp_src =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtcp_src_1");

    GstElement* udp_audio_send = gst_element_factory_make("udpsink", NULL);
    gst_bin_add(pthis->bin, udp_audio_send);
    g_object_set(G_OBJECT(udp_audio_send), "sync", FALSE, NULL);
    g_object_set(G_OBJECT(udp_audio_send), "host",
                 pthis->config.audio_send_rtp_host, NULL);
    g_object_set(G_OBJECT(udp_audio_send),
                 "port", pthis->config.audio_send_rtp_port, NULL);
    g_object_set(G_OBJECT(udp_audio_send),
                 "bind-port", pthis->config.audio_recv_rtp_port, NULL);

    GstPad* udp_audio_send_sink =
    gst_element_get_static_pad(udp_audio_send, "sink");
    pad_link = gst_pad_link(audio_send_rtp_src, udp_audio_send_sink);
    g_assert(!pad_link);

    GstElement* udp_audio_rtcp_send = gst_element_factory_make("udpsink", NULL);
    gst_bin_add(pthis->bin, udp_audio_rtcp_send);
    g_object_set(G_OBJECT(udp_audio_rtcp_send), "sync", FALSE, NULL);
    g_object_set(G_OBJECT(udp_audio_rtcp_send), "host",
                 pthis->config.audio_send_rtp_host, NULL);
    g_object_set(G_OBJECT(udp_audio_rtcp_send),
                 "port", pthis->config.audio_send_rtcp_port, NULL);
    g_object_set(G_OBJECT(udp_audio_rtcp_send),
                 "bind-port", pthis->config.audio_recv_rtcp_port, NULL);

    GstPad* udp_audio_rtcp_send_sink =
    gst_element_get_static_pad(udp_audio_rtcp_send, "sink");
    pad_link = gst_pad_link(audio_send_rtcp_src, udp_audio_rtcp_send_sink);
    g_assert(!pad_link);
  }

  return 0;
}

GstBin* rtp_relay_get_bin(struct rtp_relay_s* pthis) {
  return pthis->bin;
}

int rtp_relay_set_send_video_src(struct rtp_relay_s* pthis,
                                 GstPad* video_src, GstCaps* caps)
{
  // TODO: assert caps compatibility!

  GstElement* h264_pay = gst_element_factory_make("rtph264pay", NULL);
  gboolean ret = gst_bin_add(pthis->bin, h264_pay);
  g_assert(ret);

  g_object_set(G_OBJECT(h264_pay), "pt", pthis->config.video_pt, NULL);
  g_object_set(G_OBJECT(h264_pay), "config-interval", 2, NULL);
  g_object_set(G_OBJECT(h264_pay), "ssrc", pthis->config.video_ssrc, NULL);

  GstPad* h264_rtp_src = gst_element_get_static_pad(h264_pay, "src");
  GstPadLinkReturn link =
  gst_pad_link(h264_rtp_src, pthis->video_send_rtp_sink);
  g_assert(!link);

  GstPad* h264_rtp_sink = gst_element_get_static_pad(h264_pay, "sink");
  GstPad* h264_rtp_ghost =
  gst_ghost_pad_new("h264_sink", h264_rtp_sink);
  gst_element_add_pad(GST_ELEMENT(pthis->bin), h264_rtp_ghost);
  link = gst_pad_link(video_src, h264_rtp_ghost);
  g_assert(!link);

  return 0;
}

int rtp_relay_set_send_audio_src(struct rtp_relay_s* pthis,
                                 GstPad* audio_src, GstCaps* caps)
{
  // TODO: assert caps compatibility!

  GstElement* opus_enc = gst_element_factory_make("opusenc", NULL);
  GstElement* opus_pay = gst_element_factory_make("rtpopuspay", NULL);
  gst_bin_add_many(pthis->bin, opus_enc, opus_pay, NULL);
  g_object_set(G_OBJECT(opus_pay), "pt", pthis->config.audio_pt, NULL);
  g_object_set(G_OBJECT(opus_pay), "ssrc", pthis->config.audio_ssrc, NULL);

  GstPad* opus_rtp_src = gst_element_get_static_pad(opus_pay, "src");
  GstPadLinkReturn link =
  gst_pad_link(opus_rtp_src, pthis->audio_send_rtp_sink);
  g_assert(!link);

  GstPad* opus_sink = gst_element_get_static_pad(opus_enc, "sink");
  GstPad* opus_rtp_ghost =
  gst_ghost_pad_new("opus_sink", opus_sink);
  gst_element_add_pad(GST_ELEMENT(pthis->bin), opus_rtp_ghost);
  link = gst_pad_link(audio_src, opus_rtp_ghost);
  g_assert(!link);
  return 0;
}

int rtp_relay_set_recv_video_src(struct rtp_relay_s* pthis,
                                 GstPad* src, GstCaps* caps)
{

  return 0;
}

int rtp_relay_set_recv_audio_src(struct rtp_relay_s* pthis,
                                 GstPad* src, GstCaps* caps)
{
  return 0;
}

