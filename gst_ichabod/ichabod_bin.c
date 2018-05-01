//
//  ichabod_bin.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/12/17.
//

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <gst/gst.h>
#include "ichabod_bin.h"
#include "screencast_src.h"
#include "horseman.h"
#include "ichabod_sinks.h"

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
  GstElement* audio_enc_tee;
  GstElement* audio_raw_tee;

  GstElement* fake_mux_vsink;
  GstElement* fake_mux_asink;

  struct rtp_relay_s* rtp_relay;
  struct screencast_src_s* screencast_src;
  struct horseman_s* horseman;
};

static int setup_bin(struct ichabod_bin_s* pthis);
static gboolean on_gst_bus(GstBus* bus, GstMessage* msg, gpointer data);
static GstPadProbeReturn on_audio_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);
static GstPadProbeReturn on_video_live
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);
static GstPadProbeReturn on_video_downstream
(GstPad *pad, GstPadProbeInfo *info, gpointer p_user);

static void on_horseman_video_frame(struct horseman_s* queue,
                                    struct horseman_frame_s* frame,
                                    void* p)
{
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*)p;
  if (frame->eos) {
    g_print("ichabod_bin: sending pipeline end-of-stream\n");
    // We can send EOS to vsrc, but it doesn't seem to be enough to interrupt
    // the audio src.
     screencast_src_send_eos(pthis->screencast_src);
    // So instead, we just eos the whole pipeline.
    //gst_element_send_event(pthis->pipeline, gst_event_new_eos());
  } else {
    screencast_src_push_frame(pthis->screencast_src,
                              frame->timestamp,
                              frame->sz_data);
  }
}

static void on_horseman_output_request(struct horseman_s* horseman,
                                       struct horseman_output_s* output,
                                       void* p)
{
  g_print("ichabod_bin: received output request type %d\n",
          output->output_type);
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*)p;
  switch (output->output_type) {
    case horseman_output_type_file:
      ichabod_attach_file(pthis, output->location);
      break;
    case horseman_output_type_rtmp:
      ichabod_attach_rtmp(pthis, output->location);
      break;
    default:
      g_print("ichabod_bin: WARNING: unknown output type request\n");
      break;
  }

  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pthis->pipeline),
                            GST_DEBUG_GRAPH_SHOW_ALL,
                            "pipeline");

}

void ichabod_bin_alloc(struct ichabod_bin_s** ichabod_bin_out) {
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*)
  calloc(1, sizeof(struct ichabod_bin_s));
  g_mutex_init(&pthis->lock);
  pthis->audio_ready = FALSE;
  pthis->video_ready = FALSE;

  struct horseman_config_s hconf;
  horseman_alloc(&pthis->horseman);
  hconf.p = pthis;
  hconf.on_video_frame = on_horseman_video_frame;
  hconf.on_output_request = on_horseman_output_request;
  horseman_load_config(pthis->horseman, &hconf);

  assert(0 == setup_bin(pthis));
  *ichabod_bin_out = pthis;
}

void ichabod_bin_free(struct ichabod_bin_s* pthis) {

  free(pthis);
}

int setup_bin(struct ichabod_bin_s* pthis) {
  /* Initialisation */
  if (!gst_is_initialized()) {
    gst_init(NULL, NULL);
  }

  screencast_src_alloc(&pthis->screencast_src);
  pthis->vsource = screencast_src_get_element(pthis->screencast_src);

  pthis->loop = g_main_loop_new(NULL, FALSE);

  /* Create gstreamer elements */
  pthis->pipeline = gst_pipeline_new("ichabod");
  pthis->mqueue_src = gst_element_factory_make("multiqueue", NULL);
  pthis->asource = gst_element_factory_make("pulsesrc", NULL);
  pthis->afps = gst_element_factory_make("audiorate", NULL);
  pthis->aconv = gst_element_factory_make("audioconvert", NULL);
  pthis->aenc = gst_element_factory_make("faac", "faaaaac");
  pthis->vfps = gst_element_factory_make("videorate", NULL);
  pthis->imgdec = gst_element_factory_make("jpegdec", NULL);
  pthis->venc = gst_element_factory_make("x264enc", NULL);
  pthis->audio_out_valve = gst_element_factory_make("valve", NULL);
  pthis->video_out_valve = gst_element_factory_make("valve", NULL);
  pthis->audio_enc_tee = gst_element_factory_make("tee", NULL);
  pthis->audio_raw_tee = gst_element_factory_make("tee", NULL);
  pthis->video_tee = gst_element_factory_make("tee", NULL);
  //pthis->fake_mux_asink = gst_element_factory_make("fakesink", NULL);
  //pthis->fake_mux_vsink = gst_element_factory_make("fakesink", NULL);

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
  //g_object_set(G_OBJECT(pthis->pipeline), "message-forward", TRUE, NULL);

  // set the input to the source element
  // not needed as long as long as we use the default source
  //g_object_set(G_OBJECT(pthis->asource), "device", "0", NULL);

  // configure source pad callbacks as a preroll workaround
  GstPad* vsrc_pad = gst_element_get_static_pad(pthis->vsource, "src");
  g_assert(vsrc_pad);
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
  // when the pipeline runs slowly
  g_object_set(G_OBJECT(pthis->asource), "buffer-time", 5000000, NULL);

  // configure video encoder
  // TODO: switch encoding settings to bitrate target if RTMP sink is attached.
  // presets indexed from x264.h x264_preset_names - ** INDEXING STARTS AT 1 **
  g_object_set(G_OBJECT(pthis->venc), "speed-preset", 1, NULL);
  // experiment: fixed keyframe rate for rtmp streams
  g_object_set(G_OBJECT(pthis->venc), "key-int-max", 60, NULL);
  // x264.tune = zerolatency
  //g_object_set(G_OBJECT(pthis->venc), "tune", 4, NULL);
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

  // raw media multiqueue
  g_object_set(G_OBJECT(pthis->mqueue_src),
               "max-size-time", 5 * GST_SECOND,
               NULL);

  // start with audio and video flows blocked from multiplexer, to allow full
  // pipeline pre-roll before writing to file
  g_object_set(G_OBJECT(pthis->video_out_valve), "drop", TRUE, NULL);
  g_object_set(G_OBJECT(pthis->audio_out_valve), "drop", TRUE, NULL);

  // Allow output sinks to run even when we have no outputs attached
//  g_object_set(G_OBJECT(pthis->audio_enc_tee), "allow-not-linked", TRUE, NULL);
//  g_object_set(G_OBJECT(pthis->video_tee), "allow-not-linked", TRUE, NULL);
//  g_object_set(G_OBJECT(pthis->fake_mux_vsink), "sync", FALSE, NULL);
//  g_object_set(G_OBJECT(pthis->fake_mux_vsink), "async", FALSE, NULL);
//  g_object_set(G_OBJECT(pthis->fake_mux_asink), "sync", FALSE, NULL);
//  g_object_set(G_OBJECT(pthis->fake_mux_asink), "async", FALSE, NULL);

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
                   pthis->audio_raw_tee,
                   pthis->aenc,
                   pthis->mqueue_src,
                   pthis->audio_out_valve,
                   pthis->video_out_valve,
                   pthis->audio_enc_tee,
                   pthis->video_tee,
                   //pthis->fake_mux_asink,
                   //pthis->fake_mux_vsink,
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
                                 pthis->audio_raw_tee,
                                 pthis->aenc,
                                 pthis->audio_out_valve,
                                 pthis->audio_enc_tee,
                                 NULL);

//  GstPad* fake_video_mux_sink =
//  gst_element_get_static_pad(pthis->fake_mux_vsink, "sink");
//  GstPad* fake_audio_mux_sink =
//  gst_element_get_static_pad(pthis->fake_mux_asink, "sink");
//  ichabod_bin_attach_mux_sink_pad(pthis, fake_audio_mux_sink,
//                                  fake_video_mux_sink);
//  gst_bin_sync_children_states(GST_BIN(pthis->pipeline));

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
  // _PASS == keep probing, _REMOVE == kill this probe
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
    g_print("Pad EOS detected. Forwarding to pipeline\n");
    // forward video eos to the rest of the pipeline
    gst_element_send_event(pthis->pipeline, gst_event_new_eos());
  }
  return GST_PAD_PROBE_PASS;
}


static gboolean on_gst_bus(GstBus* bus, GstMessage* msg, gpointer data)
{
  g_print("ichabod_bin: on_gst_bus\n");
  GMainLoop* loop = (GMainLoop*)data;

  switch (GST_MESSAGE_TYPE(msg)) {

    case GST_MESSAGE_EOS:
    {
      g_print("End of stream\n");
      g_main_loop_quit(loop);
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
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure* structure = gst_message_get_structure(msg);
      gchar* sz_struct_str = gst_structure_to_string(structure);
      g_print("element message: %s\n", sz_struct_str);
      g_free(sz_struct_str);
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
  GstStateChangeReturn result;

  result = gst_bin_sync_children_states(GST_BIN(pthis->pipeline));
  /* Set the pipeline to "playing" state */
  result = gst_element_set_state(pthis->pipeline, GST_STATE_PLAYING);

  if (GST_STATE_CHANGE_FAILURE == result) {
    g_printerr("failed to start pipeline!\n");
    return -1;
  }

  int horseman_started = horseman_start(pthis->horseman);
  assert(!horseman_started);

  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pthis->pipeline),
                            GST_DEBUG_GRAPH_SHOW_ALL,
                            "pipeline");

  // TODO: This should be in it's own thread and obey stop/start cycles.
  /* Iterate */
  g_print("Running...\n");
  g_main_loop_run(pthis->loop);

  horseman_stop(pthis->horseman);

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

/* Even internally, this should be preferred over gst_bin_add for any
 * changes that could happen after preflight has finished. Without the sync
 * call, dynamic pipeline changes will not take effect.
 */
int ichabod_bin_add_element(struct ichabod_bin_s* bin, GstElement* element) {
  gboolean ret = gst_bin_add(GST_BIN(bin->pipeline), element);
  ret = gst_element_sync_state_with_parent(element);
  return ret;
}

int ichabod_bin_attach_mux_sink_pad
(struct ichabod_bin_s* pthis, GstPad* audio_sink, GstPad* video_sink)
{
  GstPad* a_tee_src_pad =
  gst_element_get_request_pad(pthis->audio_enc_tee, "src_%u");
  GstPad* v_tee_src_pad =
  gst_element_get_request_pad(pthis->video_tee, "src_%u");

  GstElement* mqueue = gst_element_factory_make("multiqueue", NULL);
  ichabod_bin_add_element(pthis, mqueue);
  //gst_bin_add(GST_BIN(pthis->pipeline), mqueue);
  g_object_set(G_OBJECT(mqueue), "max-size-time", 10 * GST_SECOND, NULL);

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

GstPad* ichabod_bin_create_audio_src(struct ichabod_bin_s* pthis, GstCaps* caps)
{
  // TODO: If the requested src is an encoded type that we have already done,
  // then we should tee the encoder output rather than this raw format.
  for (int i = 0; i < gst_caps_get_size(caps); i++) {
    GstStructure *structure = gst_caps_get_structure(caps, i);
    const gchar* name = gst_structure_get_name(structure);
    g_print("ichabod_bin: create audio source with caps %s\n", name);
    g_assert(!strcmp("audio/x-raw", name));
  }

  GstPad* a_tee_src_pad =
  gst_element_get_request_pad(pthis->audio_raw_tee, "src_%u");

  gchar* pad_name = gst_pad_get_name(a_tee_src_pad);
  char queue_name[32];
  sprintf(queue_name, "arawqueue_%s", pad_name);
  GstElement* queue = gst_element_factory_make("queue", queue_name);
  g_object_set(G_OBJECT(queue), "max-size-time", 10 * GST_SECOND, NULL);
  ichabod_bin_add_element(pthis, queue);
  g_free(pad_name);

  GstPad* queue_sink_pad = gst_element_get_static_pad(queue, "sink");
  GstPad* queue_src_pad = gst_element_get_static_pad(queue, "src");

  GstPadLinkReturn ret = gst_pad_link(a_tee_src_pad, queue_sink_pad);
  g_assert(!ret);
  return queue_src_pad;
}

GstPad* ichabod_bin_create_video_src(struct ichabod_bin_s* pthis, GstCaps* caps)
{
  // TODO: This should support both encoded and raw video interception
  for (int i = 0; i < gst_caps_get_size(caps); i++) {
    GstStructure *structure = gst_caps_get_structure(caps, i);
    const gchar* name = gst_structure_get_name(structure);
    g_print("ichabod_bin: create video source with caps %s\n", name);
    g_assert(!strcmp("video/x-h264", name));
  }

  GstPad* v_tee_src_pad =
  gst_element_get_request_pad(pthis->video_tee, "src_%u");

  gchar* pad_name = gst_pad_get_name(v_tee_src_pad);
  char queue_name[32];
  sprintf(queue_name, "vencqueue_%s", pad_name);
  GstElement* queue = gst_element_factory_make("queue", queue_name);
  g_object_set(G_OBJECT(queue), "max-size-time", 10 * GST_SECOND, NULL);
  ichabod_bin_add_element(pthis, queue);
  g_free(pad_name);

  GstPad* queue_sink_pad = gst_element_get_static_pad(queue, "sink");
  GstPad* queue_src_pad = gst_element_get_static_pad(queue, "src");

  GstPadLinkReturn ret = gst_pad_link(v_tee_src_pad, queue_sink_pad);
  g_assert(!ret);
  return queue_src_pad;
}

void ichabod_bin_set_rtp_relay(struct ichabod_bin_s* pthis,
                               struct rtp_relay_s* rtp_relay)
{
  pthis->rtp_relay = rtp_relay;
  GstElement* relay_element = GST_ELEMENT(rtp_relay_get_bin(rtp_relay));
  gboolean ret = ichabod_bin_add_element(pthis, relay_element);
  g_assert(ret);
}
