//
//  main.c
//  gst_ichabod
//
//  Created by Charley Robinson on 9/10/17.
//

#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <gst/gst.h>
#include <glib.h>
#include "ichabod_bin.h"
#include "broadcast_sink.h"

int main(int argc, char *argv[])
{
  int c;
  char cwd[1024];
  g_print("%d\n", getpid());
  g_print("%s\n", getcwd(cwd, sizeof(cwd)));

  char* output_path = NULL;
  char* broadcast_url = NULL;
  static struct option long_options[] =
  {
    {"output", optional_argument,       0, 'o'},
    {"broadcast", optional_argument,       0, 'b'},
    {0, 0, 0, 0}
  };
  /* getopt_long stores the option index here. */
  int option_index = 0;

  while ((c = getopt_long(argc, argv, "o:b:",
                          long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'o':
        output_path = optarg;
        break;
      case 'b':
        broadcast_url = optarg;
        break;
      case '?':
        if (isprint(optopt))
          g_printerr("Unknown option `-%c'.\n", optopt);
        else
          g_printerr( "Unknown option character `\\x%x'.\n", optopt);
        return 1;
      default:
        abort ();
    }
  }

  if (!output_path) {
    output_path = "output.mp4";
  }

  struct ichabod_bin_s* ichabod_bin;
  ichabod_bin_alloc(&ichabod_bin);

  GstElement* mux = gst_element_factory_make("mp4mux", "mymux");
  GstElement* sink = gst_element_factory_make("filesink", "fsink");

  // configure multiplexer
  //g_signal_connect (mux, "pad-added",
  //                  G_CALLBACK (pad_added_handler), &ichabod);
  g_object_set(G_OBJECT(mux), "faststart", TRUE, NULL);
  //g_object_set(G_OBJECT(mux), "streamable", TRUE, NULL);

  // configure output sink
  g_object_set (G_OBJECT (sink), "location", output_path, NULL);
  //g_object_set (G_OBJECT (sink), "async", FALSE, NULL);

  int ret = ichabod_bin_add_element(ichabod_bin, mux);
  ret = ichabod_bin_add_element(ichabod_bin, sink);
  GstPad* apad = gst_element_get_request_pad(mux, "audio_%u");
  GstPad* vpad = gst_element_get_request_pad(mux, "video_%u");
  assert(!ichabod_bin_attach_mux_sink_pad(ichabod_bin, apad, vpad));
  gst_element_link(mux, sink);

  if (broadcast_url) {
    g_print("attach broadcast url %s\n", broadcast_url);
    ret = ichabod_attach_rtmp(ichabod_bin, broadcast_url);
  }

  ichabod_bin_start(ichabod_bin);
  
  return 0;
}
