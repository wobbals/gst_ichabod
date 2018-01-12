//
//  ichabod_bin.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/12/17.
//  Copyright © 2017 Charley Robinson. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <gst/gst.h>
#include "ichabod_bin.h"
#include "gsthorsemansrc.h"

struct ichabod_bin_s {
  GMainLoop *loop;
  guint bus_watch_id;

  GstElement* pipeline;
  GstElement* asource;
  GstElement* aconv;
  GstElement* afps;
  GstElement* aenc;

  GstElement* mqueue_src;

  GstElement* vsource;
  GstElement* imgdec;
  GstElement* vfps;
  GstElement* venc;

  GMutex lock;
  gboolean audio_ready;
  gboolean video_ready;
  gboolean pipe_open_requested;

  /* output chain */
  GstElement* video_out_valve;
  GstElement* video_tee;
  GstElement* audio_out_valve;
  GstElement* audio_tee;
};

static int setup_bin(struct ichabod_bin_s* pthis);
static gboolean on_gst_bus(GstBus* bus, GstMessage* msg, gpointer data);
static GstPadProbeReturn on_audio_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);
static GstPadProbeReturn on_video_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);
static GstPadProbeReturn on_video_downstream
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);

void ichabod_bin_alloc(struct ichabod_bin_s** ichabod_bin_out) {
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*)
  calloc(1, sizeof(struct ichabod_bin_s));
  g_mutex_init(&pthis->lock);
  pthis->audio_ready = FALSE;
  pthis->video_ready = FALSE;
  assert(0 == setup_bin(pthis));
  *ichabod_bin_out = pthis;
}

void ichabod_bin_free(struct ichabod_bin_s* ichabod_bin);

int setup_bin(struct ichabod_bin_s* pthis) {
  /* Initialisation */
  if (!gst_is_initialized()) {
    gst_init(NULL, NULL);
  }
  gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR,
                             "horsemansrc", "horseman video source",
                             horsemansrc_init,
                             "0.0.1",
                             "LGPL",
                             "testsrc", "testsrc",
                             "https://");

  pthis->loop = g_main_loop_new(NULL, FALSE);

  /* Create gstreamer elements */
  pthis->pipeline = gst_pipeline_new("ichabod");
  pthis->mqueue_src = gst_element_factory_make("multiqueue", "mqueue");
  pthis->asource = gst_element_factory_make("pulsesrc", "asrc-pulse");
  pthis->afps = gst_element_factory_make("audiorate", "afps");
  pthis->aconv = gst_element_factory_make("audioconvert", "audio-converter");
  pthis->aenc = gst_element_factory_make("faac", "faaaaac");
  pthis->vsource = gst_element_factory_make("horsemansrc", "horseman");
  pthis->vfps = gst_element_factory_make("videorate", "vfps");
  pthis->imgdec = gst_element_factory_make("jpegdec", "jpeg-decoder");
  pthis->venc = gst_element_factory_make("x264enc", "H.264 encoder");
  pthis->audio_out_valve = gst_element_factory_make("valve", "avalve");
  pthis->video_out_valve = gst_element_factory_make("valve", "vvalve");
  pthis->audio_tee = gst_element_factory_make("tee", "atee");
  pthis->video_tee = gst_element_factory_make("tee", "vtee");

  if (!pthis->pipeline) {
    g_printerr("pipeline alloc failure.\n");
    return -1;
  }

  if (!pthis->vsource || !pthis->imgdec || !pthis->venc)
  {
    g_printerr ("Video components missing. Check gst installation.\n");
    return -1;
  }

  if (!pthis->asource || !pthis->aconv || !pthis->aenc) {
    g_printerr("Audio components could not be created. Check gst install.\n");
    return -1;
  }

  /* Set up the pipeline */

  // set the input to the source element
  // not needed as long as long as we use the default source
  //g_object_set (G_OBJECT (source), "device", "0", NULL);

  // configure source pad callbacks as a preroll workaround
  GstPad* vsrc_pad = gst_element_get_static_pad(pthis->vsource, "src");
  gst_pad_add_probe(vsrc_pad,
                    GST_PAD_PROBE_TYPE_BLOCK |
                    GST_PAD_PROBE_TYPE_SCHEDULING |
                    GST_PAD_PROBE_TYPE_BUFFER,
                    on_video_live,
                    pthis, NULL);
  gst_pad_add_probe(vsrc_pad,
                    GST_PAD_PROBE_TYPE_BLOCK |
                    GST_PAD_PROBE_TYPE_SCHEDULING |
                    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                    on_video_downstream,
                    pthis, NULL);

  // configure audio source pad callback(s)
  GstPad* asrc_pad = gst_element_get_static_pad(pthis->asource, "src");
  gst_pad_add_probe(asrc_pad,
                    GST_PAD_PROBE_TYPE_BLOCK |
                    GST_PAD_PROBE_TYPE_SCHEDULING |
                    GST_PAD_PROBE_TYPE_BUFFER,
                    on_audio_live,
                    pthis, NULL);
  gst_object_unref(asrc_pad);
  asrc_pad = NULL;

  // give plenty of buffer tolerance to the audio source, which can get finicky
  // when the pipeline runs slowly. (read: nearly always)
  g_object_set(G_OBJECT(pthis->asource), "buffer-time", 5000000, NULL);

  // configure video encoder
  // TODO: switch encoding settings to bitrate target if RTMP sink is attached.
  // presets indexed from x264.h x264_preset_names - ** INDEXING STARTS AT 1 **
  g_object_set(G_OBJECT(pthis->venc), "speed-preset", 1, NULL);
  // pass values indexed from gstx264enc.c. Not sure how to reference the actual
  // object, so for testing purposes, 5=crf, 4=qp, 0=cbr. :-|
  g_object_set(G_OBJECT(pthis->venc), "pass", 5, NULL);
  g_object_set(G_OBJECT(pthis->venc), "qp-min", 16, NULL);
  g_object_set(G_OBJECT(pthis->venc), "qp-max", 22, NULL);
  // in crf, this sets crf. in qp, this sets qp. FUN. :-|
  g_object_set(G_OBJECT(pthis->venc), "quantizer", 18, NULL);
  // in crf, bitrate property is just a max limit to prevent runaway filesize
  g_object_set(G_OBJECT(pthis->venc), "bitrate", 4096, NULL);

  // configure constant fps filter
  // TODO: Framerate be configurable
#define OUTPUT_VIDEO_FPS 30
  // TODO: Periodically dump add/drop stats from videorate while in main loop.
  g_object_set (G_OBJECT (pthis->vfps), "max-rate", OUTPUT_VIDEO_FPS, NULL);
  g_object_set (G_OBJECT (pthis->vfps), "silent", TRUE, NULL);
  g_object_set (G_OBJECT (pthis->vfps), "skip-to-first", FALSE, NULL);

  // configure constant audio fps filter
  g_object_set (G_OBJECT (pthis->afps), "silent", TRUE, NULL);
  g_object_set (G_OBJECT (pthis->afps), "skip-to-first", FALSE, NULL);

  g_object_set(G_OBJECT(pthis->mqueue_src),
               "max-size-time", 5 * GST_SECOND,
               NULL);

  // start with audio and video flows blocked from multiplexer, to allow full
  // pipeline pre-roll before writing to file
  g_object_set(G_OBJECT(pthis->video_out_valve), "drop", TRUE, NULL);
  g_object_set(G_OBJECT(pthis->audio_out_valve), "drop", TRUE, NULL);

  /* we add a message handler */
  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pthis->pipeline));
  pthis->bus_watch_id = gst_bus_add_watch(bus, on_gst_bus, pthis->loop);
  gst_object_unref(bus);

  // add all elements into the pipeline
  gst_bin_add_many(GST_BIN (pthis-> pipeline),
                   pthis->vsource,
                   pthis->vfps,
                   pthis->imgdec,
                   pthis->venc,
                   pthis->asource,
                   pthis->afps,
                   pthis->aconv,
                   pthis->aenc,
                   pthis->mqueue_src,
                   pthis->audio_out_valve,
                   pthis->video_out_valve,
                   pthis->audio_tee,
                   pthis->video_tee,
                   NULL);

  // link video element chain
  GstPad* mqueue_sink_v_pad =
  gst_element_get_request_pad(pthis->mqueue_src, "sink_0");
  GstPad* mqueue_src_v_pad =
  gst_element_get_static_pad(pthis->mqueue_src, "src_0");
  GstPad* imgdec_sink = gst_element_get_static_pad(pthis->imgdec, "sink");

  GstPadLinkReturn link_ret = gst_pad_link(vsrc_pad, mqueue_sink_v_pad);
  link_ret = gst_pad_link(mqueue_src_v_pad, imgdec_sink);

  GstCaps* vcaps_variable_fps =
  gst_caps_new_simple("video/x-raw",
                      "framerate", GST_TYPE_FRACTION, 0, 1,
                      NULL);
  GstCaps* vcaps_constant_fps =
  gst_caps_new_simple("video/x-raw",
                      "framerate", GST_TYPE_FRACTION, OUTPUT_VIDEO_FPS, 1,
                      NULL);

  gst_element_link_filtered(pthis->imgdec, pthis->vfps, vcaps_variable_fps);
  gst_element_link_filtered(pthis->vfps, pthis->venc, vcaps_constant_fps);

  gst_caps_unref(vcaps_variable_fps);
  gst_caps_unref(vcaps_constant_fps);
  vcaps_variable_fps = NULL;
  vcaps_constant_fps = NULL;

  gboolean result = gst_element_link_many(pthis->venc,
                                          pthis->video_out_valve,
                                          pthis->video_tee,
                                          NULL);

  // audio element chain
  GstPad* mqueue_sink_a_pad =
  gst_element_get_request_pad(pthis->mqueue_src, "sink_1");
  GstPad* mqueue_src_a_pad =
  gst_element_get_static_pad(pthis->mqueue_src, "src_1");
  GstPad* aconv_sink = gst_element_get_static_pad(pthis->aconv, "sink");

  GstCaps* acaps = gst_caps_new_simple("audio/x-raw",
                                       "channels", G_TYPE_INT, 2,
                                       NULL);
  asrc_pad = gst_element_get_compatible_pad(pthis->asource,
                                            mqueue_sink_a_pad, acaps);
  link_ret = gst_pad_link(asrc_pad, mqueue_sink_a_pad);
  link_ret = gst_pad_link(mqueue_src_a_pad, aconv_sink);

  result = gst_element_link_filtered(pthis->aconv, pthis->afps, acaps);
  result = gst_element_link_many(pthis->afps,
                                 pthis->aenc,
                                 pthis->audio_out_valve,
                                 pthis->audio_tee,
                                 NULL);

  gst_caps_unref(acaps);
  acaps = NULL;

  // set a high pipeline latency tolerance because reasons
  gst_pipeline_set_latency(GST_PIPELINE(pthis->pipeline), 10 * GST_SECOND);
  return 0;
}

#pragma mark - Statics

static void open_pipeline_valves(struct ichabod_bin_s* pthis) {
  g_print("ichabod: open pipeline (sync)\n");
  g_object_set(G_OBJECT(pthis->video_out_valve), "drop", FALSE, NULL);
  g_object_set(G_OBJECT(pthis->audio_out_valve), "drop", FALSE, NULL);

}

static GstPadProbeReturn on_audio_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user)
{
  // do we need to check for audio levels as well? silent frames are sneaking in
  GstPadProbeReturn ret = GST_PAD_PROBE_PASS;
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*) p_user;
  g_mutex_lock(&pthis->lock);
  pthis->audio_ready = TRUE;
  if (pthis->audio_ready && pthis->video_ready && !pthis->pipe_open_requested) {
    pthis->pipe_open_requested = TRUE;
    open_pipeline_valves(pthis);
  }
  g_mutex_unlock(&pthis->lock);

  return ret;
}

static GstPadProbeReturn on_video_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user)
{
  // _PASS == keep probing, _DROP == kill this probe
  GstPadProbeReturn ret = GST_PAD_PROBE_PASS;
  //return GST_PAD_PROBE_REMOVE;
  struct ichabod_bin_s* pthis = p_user;
  g_mutex_lock(&pthis->lock);
  pthis->video_ready = TRUE;
  if (pthis->audio_ready && pthis->video_ready && !pthis->pipe_open_requested) {
    pthis->pipe_open_requested = TRUE;
    open_pipeline_valves(pthis);
  }
  g_mutex_unlock(&pthis->lock);

  // we just need a hook for first frame received. everything can proceed
  // as usual after this.
  return ret;
}

static GstPadProbeReturn on_video_downstream
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user)
{
  struct ichabod_bin_s* pthis = p_user;
  GstEvent* event = gst_pad_probe_info_get_event(info);
  GstEventType type = GST_EVENT_TYPE(event);
  if (GST_EVENT_EOS == type) {
    // forward video eos to the rest of the pipeline
    gst_element_send_event(pthis->pipeline, gst_event_new_eos());
  }
  return GST_PAD_PROBE_PASS;
}


static gboolean on_gst_bus(GstBus* bus, GstMessage* msg, gpointer data)
{
  GMainLoop* loop = (GMainLoop*)data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
    {
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }

    case GST_MESSAGE_STREAM_STATUS:
    {
      GstStreamStatusType type;
      GstElement* owner;
      //GstObject* src = GST_MESSAGE_SRC(msg);
      //gchar* name = gst_object_get_name(src);
      gst_message_parse_stream_status(msg, &type, &owner);
      gchar* name = gst_element_get_name(owner);
      g_print("Stream %s: Status = %d\n", name, type);
      break;
    }

    case GST_MESSAGE_LATENCY:
    {
      g_print("ichabod: latency event\n");
      break;
    }

    case GST_MESSAGE_STATE_CHANGED:
    {
      g_print("state change\n");
      break;
    }
    default:
    {
      g_print("other event (%s)\n", GST_MESSAGE_TYPE_NAME(msg));
      break;
    }
  }

  return TRUE;
}

#pragma mark - external bin control

int ichabod_bin_start(struct ichabod_bin_s* pthis) {
  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pthis->pipeline),
                            GST_DEBUG_GRAPH_SHOW_ALL,
                            "pipeline");

  GstStateChangeReturn result;
  
  /* Set the pipeline to "playing" state */
  result = gst_element_set_state(pthis->pipeline, GST_STATE_PLAYING);

  if (GST_STATE_CHANGE_FAILURE == result) {
    g_printerr("failed to start pipeline!\n");
    return -1;
  }

  // TODO: This should be in it's own thread and obey stop/start cycles.
  /* Iterate */
  g_print("Running...\n");
  g_main_loop_run(pthis->loop);

  /* Out of the main loop, clean up nicely */
  g_print("Returned, stopping playback\n");
  gst_element_set_state(pthis->pipeline, GST_STATE_NULL);

  g_print("Deleting pipeline\n");
  gst_object_unref(GST_OBJECT(pthis->pipeline));
  g_source_remove(pthis->bus_watch_id);
  g_main_loop_unref(pthis->loop);

  return 0;
}

int ichabod_bin_stop(struct ichabod_bin_s* ichabod_bin);

int ichabod_bin_add_element(struct ichabod_bin_s* bin, GstElement* element) {
  // Consider making a virtual pad for the whole bin rather than forcing
  // downstream to add elements to this pipeline in order to extend it.
  return !gst_bin_add(GST_BIN(bin->pipeline), element);
}

int ichabod_bin_attach_mux_sink_pad
(struct ichabod_bin_s* pthis, GstPad* audio_sink, GstPad* video_sink)
{
  GstPad* a_tee_src_pad =
  gst_element_get_request_pad(pthis->audio_tee, "src_%u");
  GstPad* v_tee_src_pad =
  gst_element_get_request_pad(pthis->video_tee, "src_%u");

  gchar* pad_name = gst_pad_get_name(a_tee_src_pad);
  char queue_name[32];
  sprintf(queue_name, "mqueue_%s", pad_name);
  GstElement* mqueue = gst_element_factory_make("multiqueue", queue_name);
  gst_bin_add(GST_BIN(pthis->pipeline), mqueue);
  g_object_set(G_OBJECT(mqueue), "max-size-time", 10 * GST_SECOND, NULL);
  g_free(pad_name);

  GstPad* mqueue_v_sink_pad = gst_element_get_request_pad(mqueue, "sink_0");
  GstPad* mqueue_v_src_pad = gst_element_get_static_pad(mqueue, "src_0");
  GstPad* mqueue_a_sink_pad = gst_element_get_request_pad(mqueue, "sink_1");
  GstPad* mqueue_a_src_pad = gst_element_get_static_pad(mqueue, "src_1");

  GstPadLinkReturn aq_ret = gst_pad_link(a_tee_src_pad, mqueue_a_sink_pad);
  GstPadLinkReturn as_ret = gst_pad_link(mqueue_a_src_pad, audio_sink);
  GstPadLinkReturn vq_ret = gst_pad_link(v_tee_src_pad, mqueue_v_sink_pad);
  GstPadLinkReturn vs_ret = gst_pad_link(mqueue_v_src_pad, video_sink);

  gst_object_unref(a_tee_src_pad);
  gst_object_unref(v_tee_src_pad);
  return aq_ret & as_ret & vq_ret & vs_ret;
}

