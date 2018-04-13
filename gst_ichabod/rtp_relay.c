//
//  rtp_recv.c
//  gst_ichabod
//
//  Created by Charley Robinson on 4/5/18.
//

#include <gio/gio.h>
#include <gst/rtp/rtp.h>
#include "rtp_relay.h"
#include "webrtc_relay.h"

struct rtp_relay_s {
  struct rtp_relay_config_s config;
  GstBin* bin;
  GstElement* rtpbin;

  GstPad* video_send_rtp_sink;
  GstPad* audio_send_rtp_sink;

  GstCaps* video_recv_caps;
  GstCaps* video_send_caps;
  GstCaps* audio_caps;
  GSocket* video_rtp_socket;
  GSocket* video_rtcp_socket;
  GSocket* audio_rtp_socket;
  GSocket* audio_rtcp_socket;

  GstPad* video_recv_src;
  GstPad* audio_recv_src;

  struct webrtc_relay_s* webrtc_relay;
};

static void on_rtpbin_pad_added(GstElement * element, GstPad * new_pad,
                                struct rtp_relay_s* pthis);
static GstCaps* request_pt_map(GstElement * rtpbin, guint session, guint pt,
                               struct rtp_relay_s* pthis);
static void on_jitterbuffer_added(GstElement* rtpbin,
                                  GstElement* jitterbuffer,
                                  guint session,
                                  guint ssrc,
                                  struct rtp_relay_s* pthis);
static GstPad* create_h264_recv_chain(struct rtp_relay_s* pthis);
static GstPad* create_opus_recv_chain(struct rtp_relay_s* pthis);
static void maybe_create_webrtc_relay(struct rtp_relay_s* pthis);

void rtp_relay_alloc(struct rtp_relay_s** rtp_recv_out) {
  struct rtp_relay_s* pthis = (struct rtp_relay_s*)
  calloc(1, sizeof(struct rtp_relay_s));

  pthis->bin = GST_BIN(gst_bin_new("rtp_relay"));
  pthis->rtpbin = gst_element_factory_make("rtpbin", NULL);
  g_object_set(G_OBJECT(pthis->rtpbin),
               //"do-retransmission", TRUE,
               "rtp-profile", GST_RTP_PROFILE_AVPF,
               "buffer-mode", 0 /* where is the header for this symbol??? */,
               NULL);
  gboolean ret = gst_bin_add(pthis->bin, pthis->rtpbin);
  g_assert(ret);

  g_signal_connect(pthis->rtpbin, "pad-added",
                   G_CALLBACK(on_rtpbin_pad_added), pthis);

  g_signal_connect(pthis->rtpbin, "request-pt-map",
                   G_CALLBACK(request_pt_map), pthis);

  g_signal_connect(pthis->rtpbin, "new-jitterbuffer",
                   G_CALLBACK(on_jitterbuffer_added), pthis);

  *rtp_recv_out = pthis;
}

void rtp_relay_free(struct rtp_relay_s* pthis) {
  gst_object_unref(pthis->bin);
  pthis->bin = NULL;
  free(pthis);
}

static GstCaps* request_pt_map(GstElement* rtpbin, guint session, guint pt,
                               struct rtp_relay_s* pthis)
{
  g_print("rtp_relay: request_pt_map: pt=%u session=%u\n", pt, session);
  if (pthis->config.video_pt == pt) {
    return pthis->video_recv_caps;
  } else if (pthis->config.audio_pt == pt) {
    return pthis->audio_caps;
  } else {
    return NULL;
  }
}

static void on_rtpbin_pad_added(GstElement* element, GstPad* new_pad,
                                struct rtp_relay_s* pthis)
{
  g_assert(element == pthis->rtpbin);
  gchar* name = gst_pad_get_name(new_pad);
  g_print("rtp_relay: rtpbin pad added: %s\n", name);

  if (g_str_has_prefix(name, "recv_rtp_src_0")) {
    GstPad* sink = create_h264_recv_chain(pthis);
    GstPadLinkReturn link = gst_pad_link(new_pad, sink);
    g_assert(!link);

    maybe_create_webrtc_relay(pthis);
  } else if (g_str_has_prefix(name, "recv_rtp_src_1")) {
    pthis->audio_recv_src = new_pad;

    // temporarily receive RTP to fakesink until webrtcbin can take over.
    GstElement* fakesink = gst_element_factory_make("fakesink", NULL);
    gst_bin_add(pthis->bin, fakesink);
    gst_element_sync_state_with_parent(fakesink);
    GstPad* fakepad = gst_element_get_static_pad(fakesink, "sink");
    GstPadLinkReturn link = gst_pad_link(new_pad, fakepad);
    g_assert(!link);

    maybe_create_webrtc_relay(pthis);
  }
}

static void on_jitterbuffer_added(GstElement* rtpbin,
                                  GstElement* jitterbuffer,
                                  guint session,
                                  guint ssrc,
                                  struct rtp_relay_s* pthis)
{
  g_print("rtp_relay: configure jitterbuffer for session %d\n", session);
  g_object_set(G_OBJECT(jitterbuffer),
               "latency", 200,
               NULL);
}


static GstElement* create_udp_src(int port, GSocket* socket) {
  GstElement* src = gst_element_factory_make("udpsrc", NULL);
  //g_object_set(G_OBJECT(src), "timeout", 10 * GST_SECOND, NULL);
  g_object_set(G_OBJECT(src),
               "port", port,
               "close-socket", FALSE,
               "socket", socket,
               NULL);
  return src;
}

static GstElement* create_udp_sink(const char* host, int port, int bind,
                                   GSocket* socket)
{
  GstElement* sink = gst_element_factory_make("udpsink", NULL);
  g_object_set(G_OBJECT(sink),
               "sync", FALSE,
               "async", FALSE,
               "host", host,
               "port", port,
               "bind-port", bind,
               "close-socket", FALSE,
               "socket", socket,
               NULL);
  return sink;
}

static void maybe_create_webrtc_relay(struct rtp_relay_s* pthis) {
  if (!pthis->audio_recv_src || !pthis->video_recv_src || pthis->webrtc_relay) {
    return;
  }
  struct webrtc_relay_config_s config;
  config.audio_caps = pthis->audio_caps;
  config.video_caps = pthis->video_send_caps;
  config.audio_rtp_src = pthis->audio_recv_src;
  config.video_rtp_src = pthis->video_recv_src;
  webrtc_relay_alloc(&pthis->webrtc_relay);
  GstBin* bin = webrtc_relay_get_bin(pthis->webrtc_relay);
  gst_bin_add(pthis->bin, bin);
  // TODO: this is too sensitive to order of operations. bin needs elements
  // before linking can occur.
  webrtc_relay_config(pthis->webrtc_relay, &config);
  gst_bin_sync_children_states(pthis->bin);

  GST_DEBUG_BIN_TO_DOT_FILE(gst_object_get_parent(GST_ELEMENT(pthis->bin)),
                            GST_DEBUG_GRAPH_SHOW_ALL,
                            "pipeline_rtc");
}

static GstPad* create_h264_recv_chain(struct rtp_relay_s* pthis) {
  GstElement* queue = gst_element_factory_make("queue", NULL);
  GstElement* depacketizer = gst_element_factory_make("rtph264depay", NULL);
  GstElement* parser = gst_element_factory_make("h264parse", NULL);
  g_object_set(G_OBJECT(parser), "config-interval", 1, NULL);
  GstElement* decoder = gst_element_factory_make("avdec_h264", NULL);
  GstElement* encoder = gst_element_factory_make("vp8enc", NULL);
  g_object_set(G_OBJECT(encoder),
               "keyframe-max-dist", 15,
               "deadline", 1,
               NULL);
  GstElement* packetizer = gst_element_factory_make("rtpvp8pay", NULL);

  gst_bin_add_many(pthis->bin, queue, depacketizer, parser, decoder,
                   encoder, packetizer,
                   NULL);
  gst_element_link_many(queue, depacketizer, parser, decoder,
                        encoder, packetizer,
                        NULL);

  pthis->video_recv_src = gst_element_get_static_pad(packetizer, "src");
  g_assert(pthis->video_recv_src);
  pthis->video_send_caps =
  gst_caps_new_simple("application/x-rtp",
                      "media", G_TYPE_STRING, "video",
                      "encoding-name", G_TYPE_STRING, "VP8",
                      "clock-rate", G_TYPE_INT, 90000,
                      "payload", G_TYPE_INT, 96,
                      NULL);

  g_assert(pthis->video_send_caps);

  gst_bin_sync_children_states(pthis->bin);
  GstPad* sink = gst_element_get_static_pad(queue, "sink");
  return sink;
}

static GstPad* create_opus_recv_chain(struct rtp_relay_s* pthis) {
  GstElement* queue = gst_element_factory_make("queue", NULL);
  GstElement* depacketizer = gst_element_factory_make("rtpopusdepay", NULL);
  GstElement* parser = gst_element_factory_make("opusparse", NULL);
  GstElement* muxer = gst_element_factory_make("oggmux", NULL);
  GstElement* filesink = gst_element_factory_make("filesink", NULL);
  g_object_set(G_OBJECT(filesink),
               "sync", FALSE,
               "async", FALSE,
               "location", "/tmp/audio_recv.ogg",
               NULL);
  gst_bin_add_many(pthis->bin,
                   queue, depacketizer, parser, muxer, filesink,
                   NULL);
  gst_element_link_many(queue, depacketizer, parser, muxer, filesink, NULL);
  gst_bin_sync_children_states(pthis->bin);
  GstPad* sink = gst_element_get_static_pad(queue, "sink");
  return sink;
}

static GSocket* local_socket_new(int port) {
  GError* error = NULL;
  GInetAddress *addr;
  GSocketAddress* sock_addr;
  GSocket* socket = g_socket_new(G_SOCKET_FAMILY_IPV4,
                                 G_SOCKET_TYPE_DATAGRAM,
                                 G_SOCKET_PROTOCOL_UDP, &error);
  g_assert(!error);
  addr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
  g_assert(addr);
  sock_addr =
  g_inet_socket_address_new(addr, port);
  g_assert(sock_addr);
  g_socket_bind(socket, sock_addr, TRUE, &error);
  g_assert(!error);
  g_object_unref(addr);
  g_object_unref(sock_addr);
  return socket;
}

static void create_udp_sockets(struct rtp_relay_s* pthis) {
  pthis->video_rtp_socket =
  local_socket_new(pthis->config.video_recv_rtp_port);
  pthis->video_rtcp_socket =
  local_socket_new(pthis->config.video_recv_rtcp_port);
  pthis->audio_rtp_socket =
  local_socket_new(pthis->config.audio_recv_rtp_port);
  pthis->audio_rtcp_socket =
  local_socket_new(pthis->config.audio_recv_rtcp_port);
}

int rtp_relay_config(struct rtp_relay_s* pthis,
                    struct rtp_relay_config_s* config)
{
  GstPadLinkReturn pad_link;
  memcpy(&pthis->config, config, sizeof(struct rtp_relay_config_s));

  create_udp_sockets(pthis);

  if (pthis->config.recv_enabled) {
    GstCaps* generic_rtcp_caps = gst_caps_from_string("application/x-rtcp");

    // TODO: Wire up the SDP and parse this stuff out.
    pthis->video_recv_caps =
    gst_caps_new_simple("application/x-rtp",
                        "media", G_TYPE_STRING, "video",
                        "encoding-name", G_TYPE_STRING, "H264",
                        "clock-rate", G_TYPE_INT, 90000,
                        NULL);
    pthis->audio_caps =
    gst_caps_new_simple("application/x-rtp",
                        "media", G_TYPE_STRING, "audio",
                        "encoding-name", G_TYPE_STRING, "OPUS",
                        "clock-rate", G_TYPE_INT, 48000,
                        NULL);

    GstPad* rtp_video_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtp_sink_0");
    GstElement* udp_video_recv_rtp =
    create_udp_src(config->video_recv_rtp_port, pthis->video_rtp_socket);
    g_object_set(G_OBJECT(udp_video_recv_rtp),
                 "caps", pthis->video_recv_caps,
                 NULL);
    gst_bin_add(pthis->bin, udp_video_recv_rtp);

    GstPad* udp_video_rtp_src =
    gst_element_get_static_pad(udp_video_recv_rtp, "src");
    pad_link = gst_pad_link(udp_video_rtp_src, rtp_video_sink);
    g_assert(!pad_link);

    GstPad* rtcp_video_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtcp_sink_0");
    GstElement* udp_video_recv_rtcp =
    create_udp_src(config->video_recv_rtcp_port, pthis->video_rtp_socket);
    g_object_set(G_OBJECT(udp_video_recv_rtcp),
                 "caps", generic_rtcp_caps,
                 NULL);
    gst_bin_add(pthis->bin, udp_video_recv_rtcp);

    GstPad* udp_video_rtcp_src =
    gst_element_get_static_pad(udp_video_recv_rtcp, "src");
    pad_link = gst_pad_link(udp_video_rtcp_src, rtcp_video_sink);
    g_assert(!pad_link);

    GstPad* rtp_audio_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtp_sink_1");
    GstElement* udp_audio_recv_rtp =
    create_udp_src(config->audio_recv_rtp_port, pthis->audio_rtp_socket);
    g_object_set(G_OBJECT(udp_audio_recv_rtp),
                 "caps", pthis->audio_caps,
                 NULL);
    gst_bin_add(pthis->bin, udp_audio_recv_rtp);

    GstPad* udp_audio_rtp_src =
    gst_element_get_static_pad(udp_audio_recv_rtp, "src");
    pad_link = gst_pad_link(udp_audio_rtp_src, rtp_audio_sink);
    g_assert(!pad_link);

    GstPad* rtcp_audio_sink =
    gst_element_get_request_pad(pthis->rtpbin, "recv_rtcp_sink_1");
    GstElement* udp_audio_recv_rtcp =
    create_udp_src(config->audio_recv_rtcp_port, pthis->audio_rtcp_socket);
    g_object_set(G_OBJECT(udp_audio_recv_rtcp),
                 "caps", generic_rtcp_caps,
                 NULL);
    gst_bin_add(pthis->bin, udp_audio_recv_rtcp);

    GstPad* udp_audio_rtcp_src =
    gst_element_get_static_pad(udp_audio_recv_rtcp, "src");
    pad_link = gst_pad_link(udp_audio_rtcp_src, rtcp_audio_sink);
    g_assert(!pad_link);

    gst_caps_unref(generic_rtcp_caps);
  }

  if (pthis->config.send_enabled) {
    pthis->video_send_rtp_sink =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtp_sink_0");
    GstPad* video_send_rtp_src =
    gst_element_get_static_pad(pthis->rtpbin, "send_rtp_src_0");
    GstPad* video_send_rtcp_src =
    gst_element_get_request_pad(pthis->rtpbin, "send_rtcp_src_0");

    GstElement* udp_video_rtp_send =
    create_udp_sink(pthis->config.video_send_rtp_host,
                    pthis->config.video_send_rtp_port,
                    pthis->config.video_recv_rtp_port,
                    pthis->video_rtp_socket);
    gst_bin_add(pthis->bin, udp_video_rtp_send);

    GstElement* udp_video_rtcp_send =
    create_udp_sink(pthis->config.video_send_rtp_host,
                    pthis->config.video_send_rtcp_port,
                    pthis->config.video_recv_rtcp_port,
                    pthis->video_rtcp_socket);
    gst_bin_add(pthis->bin, udp_video_rtcp_send);

    GstElement* udp_audio_rtp_send =
    create_udp_sink(pthis->config.audio_send_rtp_host,
                    pthis->config.audio_send_rtp_port,
                    pthis->config.audio_recv_rtp_port,
                    pthis->audio_rtp_socket);
    gst_bin_add(pthis->bin, udp_audio_rtp_send);

    GstElement* udp_audio_rtcp_send =
    create_udp_sink(pthis->config.audio_send_rtp_host,
                    pthis->config.audio_send_rtcp_port,
                    pthis->config.audio_recv_rtcp_port,
                    pthis->audio_rtcp_socket);
    gst_bin_add(pthis->bin, udp_audio_rtcp_send);

    GstPad* udp_video_rtp_send_sink =
    gst_element_get_static_pad(udp_video_rtp_send, "sink");
    pad_link = gst_pad_link(video_send_rtp_src, udp_video_rtp_send_sink);
    g_assert(!pad_link);

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

    GstPad* udp_audio_send_sink =
    gst_element_get_static_pad(udp_audio_rtp_send, "sink");
    pad_link = gst_pad_link(audio_send_rtp_src, udp_audio_send_sink);
    g_assert(!pad_link);

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

  gboolean sync = gst_element_sync_state_with_parent(GST_ELEMENT(pthis->bin));
  g_assert(sync);
  return 0;
}

int rtp_relay_set_send_audio_src(struct rtp_relay_s* pthis,
                                 GstPad* audio_src, GstCaps* caps)
{
  // TODO: assert caps compatibility!

  GstElement* opus_enc = gst_element_factory_make("opusenc", NULL);
  GstElement* opus_pay = gst_element_factory_make("rtpopuspay", NULL);
  gst_bin_add_many(pthis->bin, opus_enc, opus_pay, NULL);
  gst_element_link(opus_enc, opus_pay);
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

  gboolean sync = gst_element_sync_state_with_parent(GST_ELEMENT(pthis->bin));
  g_assert(sync);
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

