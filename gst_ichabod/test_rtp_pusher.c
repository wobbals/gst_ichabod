//
//  test_rtp_pusher.c
//  gst_ichabod
//
//  Created by Charley Robinson on 3/28/18.
//  Copyright © 2018 Charley Robinson. All rights reserved.
//

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <gst/gst.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#include "webrtc_control.h"

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1;
static struct webrtc_control_s* rtc_ctrl;

static void on_incoming_stream
(GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
  g_print("on_incoming_stream\n");
}

static void send_ice_candidate_message
(GstElement* webrtc G_GNUC_UNUSED, guint mlineindex, gchar* candidate,
 gpointer user_data G_GNUC_UNUSED)
{
  g_print("send_ice_candidate_message: %d\n", mlineindex);
  webrtc_control_send_ice_candidate(rtc_ctrl, mlineindex, candidate);
}

/* Offer created by our pipeline, to be sent to the peer */
static void on_offer_created(GstPromise * promise, gpointer user_data)
{
  g_print("on_offer_created\n");

  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;

  g_assert_cmphex (gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
                     GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);
  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  gchar* sz_sdp = gst_sdp_message_as_text(offer->sdp);
  webrtc_control_send_offer(rtc_ctrl, sz_sdp);
  gst_webrtc_session_description_free (offer);
  free(sz_sdp);
}

static void on_negotiation_needed (GstElement * element, gpointer user_data)
{
  GArray *transceivers;
  g_signal_emit_by_name (webrtc1, "get-transceivers", &transceivers);
//  // TODO: This hack is from another. Fix it when rtptransciever has setter for
//  // direction.
//  for (int i = 0; i < transceivers->len; i++) {
//    GstWebRTCRTPTransceiver* trans = g_array_index(transceivers,
//                                                   GstWebRTCRTPTransceiver*, 1);
//    trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
//    trans->current_direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
//  }
  GstPromise *promise;
  promise = gst_promise_new_with_change_func(on_offer_created, user_data, NULL);
  g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
}

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

static gboolean start_pipeline(void)
{
  GstStateChangeReturn ret;
  GError *error = NULL;

  pipe1 =
  gst_parse_launch("webrtcbin name=sendrecv " STUN_SERVER
                   "filesrc "
                   "location=/var/lib/rtpPusher/keyboardcat_baseline.mp4 "
                   //"location=/Users/charley/src/wormhole/tools/keyboardcat_baseline.mp4 "
                   "! queue ! qtdemux name=demux demux. "
                   //name=remotesrc uri=file:///Users/charley/src/wormhole/tools/keyboardcat_baseline.mp4 "
                   "! queue ! avdec_h264 ! vp8enc keyframe-max-dist=15 deadline=1 ! rtpvp8pay ! "
                   "queue ! " RTP_CAPS_VP8 "96 ! sendrecv. "
                   "demux. ! faad ! audioresample ! audio/x-raw, rate=48000 ! opusenc ! rtpopuspay ! "
                   "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. "
                   , &error);

  if (error) {
    g_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "sendrecv");
  g_assert_nonnull (webrtc1);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc1, "on-negotiation-needed",
                    G_CALLBACK (on_negotiation_needed), NULL);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc1, "on-ice-candidate",
                    G_CALLBACK (send_ice_candidate_message), NULL);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream),
                    pipe1);
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1);

  g_print ("Starting pipeline\n");
  ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipe1),
                            GST_DEBUG_GRAPH_SHOW_ALL,
                            "webrtcpipe");

  return TRUE;

err:
  if (pipe1)
    g_clear_object (&pipe1);
  if (webrtc1)
    webrtc1 = NULL;
  return FALSE;
}

static gboolean
check_plugins (void)
{
  int i;
  gboolean ret;
  GstPlugin *plugin;
  GstRegistry *registry;
  const gchar *needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
    "rtpmanager", "videotestsrc", "audiotestsrc", NULL};

  registry = gst_registry_get ();
  ret = TRUE;
  for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      g_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
      ret = FALSE;
      continue;
    }
    gst_object_unref (plugin);
  }
  return ret;
}

void on_create_offer(struct webrtc_control_s* webrtc_control, void* p) {
  start_pipeline();
}

void on_remote_answer(struct webrtc_control_s* webrtc_control,
                      const char* remote_answer,
                      void* p)
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
  g_signal_emit_by_name (webrtc1, "set-remote-description", answer,
                         promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);
}

void on_remote_candidate(struct webrtc_control_s* webrtc_control,
                         int8_t m_line_index, const char* candidate,
                         void* p)
{
  /* Add ice candidate sent by remote peer */
  g_signal_emit_by_name(webrtc1, "add-ice-candidate", m_line_index, candidate);
}

int main(int argc, char** argv)
{
  char cwd[1024];
  g_print("%d\n", getpid());
  g_print("%s\n", getcwd(cwd, sizeof(cwd)));

  GError *error = NULL;
  gst_init(NULL, NULL);

  if (!check_plugins ()) {
    return -1;
  }

  struct webrtc_control_config_s webrtc_config;
  webrtc_config.on_create_offer = on_create_offer;
  webrtc_config.on_remote_answer = on_remote_answer;
  webrtc_config.on_remote_candidate = on_remote_candidate;
  webrtc_config.p = 0xdeadbeef;
  webrtc_control_alloc(&rtc_ctrl);
  webrtc_control_config(rtc_ctrl, &webrtc_config);
  webrtc_control_start(rtc_ctrl);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
  g_print ("Pipeline stopped\n");

  gst_object_unref (pipe1);

  return 0;
}
