
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

  static gboolean
  bus_call (GstBus     *bus,
            GstMessage *msg,
            gpointer    data)
  {
    GMainLoop *loop = (GMainLoop *) data;
    
    switch (GST_MESSAGE_TYPE (msg)) {
        
      case GST_MESSAGE_EOS:
        g_print ("End of stream\n");
        g_main_loop_quit (loop);
        break;
        
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
      default:
        break;
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


  GstElement *pipeline;

  void on_interrupt(int sig) {
    if (pipeline) {
      g_print("send eos\n");
      gst_element_send_event (pipeline, gst_event_new_eos());
    }
  }

  int
  main (int   argc,
        char *argv[])
  {
    signal(SIGINT, on_interrupt);

    char cwd[1024];
    g_print("%d\n", getpid());
    g_print("%s\n", getcwd(cwd, sizeof(cwd)));
    
    GMainLoop *loop;
    GstElement *vsource, *fps, *imgdec, *venc, *mux, *sink;
    GstElement *asource, *aconv, *aenc;
    GstBus *bus;
    guint bus_watch_id;
    
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
    pipeline = gst_pipeline_new ("ichabod");
    //asource   = gst_element_factory_make("pulsesrc", "pulse-audio");
    //aconv     = gst_element_factory_make ("audioconvert", "audio-converter");
    //aenc      = gst_element_factory_make ("faac", "faaaaac");
    vsource   = gst_element_factory_make ("horsemansrc", "horseman");
    fps       = gst_element_factory_make ("videorate", "constant-fps");
    imgdec    = gst_element_factory_make ("jpegdec", "jpeg-decoder");
    venc      = gst_element_factory_make ("x264enc", "H.264 encoder");
    mux       = gst_element_factory_make ("mp4mux", "mymux");
    sink      = gst_element_factory_make ("filesink", "fsink");

    if (!pipeline || !vsource || !imgdec || !venc || !fps || !mux || !sink) {
      g_printerr ("One element could not be created. Exiting.\n");
      return -1;
    }
    
    /* Set up the pipeline */
    
    // set the input to the source element
    // not needed as long as long as we use the default source
    //g_object_set (G_OBJECT (source), "device", "0", NULL);

    // set the output destination
    g_object_set (G_OBJECT (sink), "location", "output.mp4", NULL);
    
    // configure video encoder
    g_object_set(G_OBJECT(venc), "speed-preset", "ultrafast", NULL);
    g_object_set(G_OBJECT(venc), "qp-min", 18, NULL);
    g_object_set(G_OBJECT(venc), "qp-max", 22, NULL);
    g_object_set(G_OBJECT(venc), "profile", "high", NULL);

    // configure constant fps filter
    g_object_set (G_OBJECT (fps), "max-rate", 30, NULL);
    g_object_set (G_OBJECT (fps), "silent", FALSE, NULL);
    g_object_set (G_OBJECT (fps), "skip-to-first", TRUE, NULL);

    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);
    
    // add all elements into the pipeline
    gst_bin_add_many (GST_BIN (pipeline),
                      vsource, fps, imgdec, venc, mux, sink, NULL);
    
    // link the elements together
    gst_element_link_many (vsource, fps, imgdec, venc, mux, sink, NULL);
    
    /* Set the pipeline to "playing" state */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    
    /* Iterate */
    g_print ("Running...\n");
    g_main_loop_run (loop);
    
    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    
    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);

    return 0;
  }
