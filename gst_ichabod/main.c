
//  main.c
//  gst_ichabod
//
//  Created by Charley Robinson on 9/10/17.
//  Copyright © 2017 Charley Robinson. All rights reserved.
//

#include <gst/gst.h>
#include <glib.h>
#include <unistd.h>
#include <signal.h>
#include "gsthorsemansrc.h"

struct ichabod_s {
  GstElement* pipeline;
  GstElement* asource;
  GstElement* aqueue;
  GstElement* aconv;
  GstElement* aenc;
  
  GstElement* vsource;
  GstElement* vqueue;
  GstElement* imgdec;
  GstElement* fps;
  GstElement* venc;
  
  GstElement* mux;
  GstElement* sink;
};
// static instance for interrupt handler.
// move this out once signals are properly handled.
static struct ichabod_s ichabod;

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;
  
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
      
    case GST_MESSAGE_STATE_CHANGED:
    {
      g_print("state change\n");
      break;
    }
    default:
    {
      g_print("other event\n");
      break;
    }
  }
  
  return TRUE;
}

static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;
  
  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");
  
  sinkpad = gst_element_get_static_pad (decoder, "sink");
  
  gst_pad_link (pad, sinkpad);
  
  gst_object_unref (sinkpad);
}

static GstPadProbeReturn on_video_live(GstPad *pad, GstPadProbeInfo *info,
                                       gpointer p_user)
{
  gboolean result;
  struct ichabod_s* pthis = p_user;
  GstPad* apad = gst_element_get_static_pad(pthis->asource, "src");
  if (!pad) {
    return GST_PAD_PROBE_OK;
  }
  result = gst_pad_is_active(apad);
  if (!result) {
    result = gst_pad_set_active(apad, TRUE);
  }
  if (!result) {
    g_printerr("failed to unpause audio source pad\n");
  }
  // we just need a hook for first frame received. everything can proceed
  // as usual herein
  return GST_PAD_PROBE_REMOVE;
}

static void on_interrupt(int sig) {
  if (ichabod.pipeline) {
    g_print("on_interrupt\n");
    gst_element_send_event (ichabod.pipeline, gst_event_new_eos());
  }
}

int
main (int   argc,
      char *argv[])
{
  
  GMainLoop *loop;
  GstElement *vsource, *fps, *imgdec, *venc, *mux, *sink;
  GstElement *asource, *aconv, *aenc;
  GstBus *bus;
  guint bus_watch_id;
  gboolean result;
  
  char cwd[1024];
  g_print("%d\n", getpid());
  g_print("%s\n", getcwd(cwd, sizeof(cwd)));
  
  //signal(SIGINT, on_interrupt);
  
  /* Initialisation */
  gst_init (&argc, &argv);
  gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR,
                             "horsemansrc", "horseman video source",
                             horsemansrc_init,
                             "0.0.1",
                             "LGPL",
                             "testsrc", "testsrc", "https://");
  
  loop = g_main_loop_new (NULL, FALSE);
  
  /* Create gstreamer elements */
  ichabod.pipeline = gst_pipeline_new ("ichabod");
  asource   = gst_element_factory_make("pulsesrc", "pulse-audio");
  ichabod.aqueue = gst_element_factory_make("queue", "aqueue");
  aconv     = gst_element_factory_make ("audioconvert", "audio-converter");
  aenc      = gst_element_factory_make ("faac", "faaaaac");
  vsource   = gst_element_factory_make ("horsemansrc", "horseman");
  ichabod.vqueue = gst_element_factory_make("queue", "vqueue");
  fps       = gst_element_factory_make ("videorate", "constant-fps");
  imgdec    = gst_element_factory_make ("jpegdec", "jpeg-decoder");
  venc      = gst_element_factory_make ("x264enc", "H.264 encoder");
  mux       = gst_element_factory_make ("mp4mux", "mymux");
  sink      = gst_element_factory_make ("filesink", "fsink");
  
  if (!ichabod.pipeline) {
    g_printerr("pipeline alloc failure.\n");
    return -1;
  }
  
  if (!vsource || !ichabod.vqueue || !imgdec || !venc || !fps || !mux || !sink)
  {
    g_printerr ("Video components missing. Check gst installation.\n");
    return -1;
  }
  
  if (!asource || !ichabod.aqueue || !aconv || !aenc) {
    g_printerr("Audio components could not be created. Check gst install.\n");
  }
  
  ichabod.asource = asource;
  ichabod.aconv = aconv;
  ichabod.aenc = aenc;
  ichabod.vsource = vsource;
  ichabod.fps = fps;
  ichabod.imgdec = imgdec;
  ichabod.venc = venc;
  ichabod.mux = mux;
  ichabod.sink = sink;
  
  /* Set up the pipeline */
  
  // set the input to the source element
  // not needed as long as long as we use the default source
  //g_object_set (G_OBJECT (source), "device", "0", NULL);
  
  // configure video source
  GstPad* vsrc_pad = gst_element_get_static_pad(vsource, "src");
  gst_pad_add_probe(vsrc_pad,
                    GST_PAD_PROBE_TYPE_BLOCK |
                    GST_PAD_PROBE_TYPE_SCHEDULING |
                    GST_PAD_PROBE_TYPE_BUFFER,
                    on_video_live,
                    &ichabod, NULL);
  
  // configure output sink
  g_object_set (G_OBJECT (sink), "location", "output.mp4", NULL);
  
  // configure video encoder
  // TODO: ultrafast not accessible by string? I'm doing something wrong here.
  g_object_set(G_OBJECT(venc), "speed-preset", 0, NULL);
  // Profile also seems ignored, while we're at it...
  g_object_set(G_OBJECT(venc), "profile", 1, NULL);
  g_object_set(G_OBJECT(venc), "qp-min", 18, NULL);
  g_object_set(G_OBJECT(venc), "qp-max", 22, NULL);
  
  // configure constant fps filter
  g_object_set (G_OBJECT (fps), "max-rate", 30, NULL);
  g_object_set (G_OBJECT (fps), "silent", FALSE, NULL);
  g_object_set (G_OBJECT (fps), "skip-to-first", TRUE, NULL);
  
  /* we add a message handler */
  bus = gst_pipeline_get_bus(GST_PIPELINE(ichabod.pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);
  
  // add all elements into the pipeline
  gst_bin_add_many(GST_BIN (ichabod.pipeline),
                   vsource, ichabod.vqueue,
                   fps, imgdec, venc, mux, sink, NULL);
  gst_bin_add_many(GST_BIN (ichabod.pipeline),
                   asource, ichabod.aqueue, aconv, aenc, NULL);

  // link the elements together
  result = gst_element_link_many(vsource, ichabod.vqueue,
                                 fps, imgdec, venc, mux, sink,
                                 NULL);
  result = gst_element_link_many(asource, ichabod.aqueue,
                                 aconv, aenc, mux, NULL);
  
  /* Set the pipeline to "playing" state */
  gst_element_set_state (ichabod.pipeline, GST_STATE_PLAYING);

  // halt audio pad until after we've got a video frame.
  GstPad* apad = gst_element_get_static_pad(asource, "src");
  result = gst_pad_set_active(apad, FALSE);
  if (!result) {
    g_printerr("failed to pause audio source pad\n");
  }

  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);
  
  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (ichabod.pipeline, GST_STATE_NULL);
  
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (ichabod.pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  
  return 0;
}
