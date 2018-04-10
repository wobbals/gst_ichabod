//
//  webrtc_relay.c
//  gst_ichabod
//
//  Created by Charley Robinson on 4/9/18.
//

#include <stdlib.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#include "webrtc_relay.h"
#include "webrtc_control.h"

struct webrtc_relay_s {
  struct webrtc_relay_config_s config;
  struct webrtc_control_s* ctrl;
  GstElement* webrtcbin;
};

void on_offer_created(GstPromise* promise, struct webrtc_relay_s* pthis);

static void send_ice_candidate_message
(GstElement* webrtc, guint mlineindex, gchar* candidate,
 struct webrtc_relay_s* pthis)
{
  g_print("send_ice_candidate_message: %d\n", mlineindex);
  webrtc_control_send_ice_candidate(pthis->ctrl, mlineindex, candidate);
}

static void on_negotiation_needed(GstElement* element,
                                  struct webrtc_relay_s* pthis)
{
//  GArray *transceivers;
//  g_signal_emit_by_name (pthis->webrtcbin, "get-transceivers", &transceivers);
//    // set this peerconnection to sendonly
//    for (int i = 0; i < transceivers->len; i++) {
//      GstWebRTCRTPTransceiver* trans =
//      g_array_index(transceivers, GstWebRTCRTPTransceiver*, 1);
//      trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
//      trans->current_direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
//    }
  GstPromise *promise;
  promise = gst_promise_new_with_change_func(on_offer_created, pthis, NULL);
  g_signal_emit_by_name(pthis->webrtcbin, "create-offer", pthis, promise);
}

void on_offer_created(GstPromise* promise, struct webrtc_relay_s* pthis) {
  g_print("webrtc_relay: on_offer_created\n");

  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;

  g_assert_cmphex (gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
                     GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);
  promise = gst_promise_new ();
  g_signal_emit_by_name(pthis->webrtcbin, "set-local-description",
                        offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  gchar* sz_sdp = gst_sdp_message_as_text(offer->sdp);
  webrtc_control_send_offer(pthis->ctrl, sz_sdp);
  gst_webrtc_session_description_free (offer);
  free(sz_sdp);
}

void on_create_offer(struct webrtc_control_s* webrtc_control,
                     struct webrtc_relay_s* pthis)
{
  // eh? is this still necessary?
  g_print("webrtc_relay: on_create_offer\n");
}

void on_remote_answer(struct webrtc_control_s* webrtc_control,
                      const char* remote_answer,
                      struct webrtc_relay_s* pthis)
{
  GstSDPMessage* sdp;
  GstWebRTCSessionDescription *answer;
  GstSDPResult ret = gst_sdp_message_new(&sdp);
  g_assert_cmphex (ret, ==, GST_SDP_OK);

  ret = gst_sdp_message_parse_buffer((guint8*)remote_answer,
                                     strlen(remote_answer), sdp);
  g_assert_cmphex (ret, ==, GST_SDP_OK);

  answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
  g_assert_nonnull (answer);

  /* Set remote description on our pipeline */
  GstPromise *promise = gst_promise_new();
  g_signal_emit_by_name (pthis->webrtcbin, "set-remote-description", answer,
                         promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

}

void on_remote_candidate(struct webrtc_control_s* webrtc_control,
                         int8_t m_line_index, const char* candidate,
                         struct webrtc_relay_s* pthis)
{
  /* Add ice candidate sent by remote peer */
  g_signal_emit_by_name(pthis->webrtcbin,
                        "add-ice-candidate", m_line_index, candidate);

}

static void on_pad_added
(GstElement* webrtc, GstPad* pad, GstElement* pipe)
{
  gchar* name = gst_pad_get_name(pad);
  g_print("webrtc_relay: on_pad_added: name=%s.\n", name);
}

void webrtc_relay_alloc(struct webrtc_relay_s** webrtc_relay_out) {
  struct webrtc_relay_s* pthis = (struct webrtc_relay_s*)
  calloc(1, sizeof(struct webrtc_relay_s));
  struct webrtc_control_config_s ctrl_config;
  ctrl_config.on_create_offer = on_create_offer;
  ctrl_config.on_remote_answer = on_remote_answer;
  ctrl_config.on_remote_candidate = on_remote_candidate;
  ctrl_config.p = pthis;
  webrtc_control_alloc(&pthis->ctrl);
  webrtc_control_config(pthis->ctrl, &ctrl_config);
  webrtc_control_start(pthis->ctrl);

  pthis->webrtcbin = gst_element_factory_make("webrtcbin", NULL);
  g_assert(pthis->webrtcbin);

  g_object_set(G_OBJECT(pthis->webrtcbin),
               "stun-server", "stun://stun.l.google.com:19302",
               NULL);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect(pthis->webrtcbin, "on-negotiation-needed",
                   G_CALLBACK(on_negotiation_needed), pthis);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect(pthis->webrtcbin, "on-ice-candidate",
                   G_CALLBACK(send_ice_candidate_message), pthis);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect(pthis->webrtcbin, "pad-added", G_CALLBACK(on_pad_added), pthis);

  *webrtc_relay_out = pthis;
}

void webrtc_relay_config(struct webrtc_relay_s* pthis,
                         struct webrtc_relay_config_s* config)
{
  memcpy(&pthis->config, config, sizeof(struct webrtc_relay_config_s));
  GstElement* video_src =
  gst_pad_get_parent_element(pthis->config.video_rtp_src);
  gboolean link = gst_element_link_filtered(video_src, pthis->webrtcbin,
                                            pthis->config.video_caps);
  g_assert(link);
  gst_object_unref(video_src);
  // pad goes to fakesink until we are ready to relay
  if (gst_pad_is_linked(pthis->config.audio_rtp_src)) {
    // TODO: release fakesink from the whole pipeline
    GstPad* orphan = gst_pad_get_peer(pthis->config.audio_rtp_src);
    gst_pad_unlink(pthis->config.audio_rtp_src, orphan);
    gst_object_unref(orphan);
  }
  GstElement* audio_src =
  gst_pad_get_parent_element(pthis->config.audio_rtp_src);
  link = gst_element_link_filtered(audio_src, pthis->webrtcbin,
                            pthis->config.audio_caps);
  gst_object_unref(audio_src);
  g_assert(link);
}

void webrtc_relay_free(struct webrtc_relay_s* pthis) {
  gst_object_unref(pthis->webrtcbin);
  pthis->webrtcbin = NULL;
  free(pthis);
}

GstBin* webrtc_relay_get_bin(struct webrtc_relay_s* pthis) {
  return pthis->webrtcbin;
}
