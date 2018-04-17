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
#include <gmodule.h>
#include "ichabod_bin.h"
#include "ichabod_sinks.h"

int main(int argc, char *argv[])
{
  int c;
  char cwd[1024];
  g_print("%d\n", getpid());
  g_print("%s\n", getcwd(cwd, sizeof(cwd)));

  GSList *broadcast_urls = NULL;
  GSList *output_paths = NULL;
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
        output_paths = g_slist_append(output_paths, optarg);
        break;
      case 'b':
        broadcast_urls = g_slist_append(broadcast_urls, optarg);
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

  if (!output_paths) {
    output_paths = g_slist_append(output_paths, "output.mp4");
  }

  struct ichabod_bin_s* ichabod_bin;
  ichabod_bin_alloc(&ichabod_bin);

  int ret = ichabod_attach_file(ichabod_bin, output_paths);
  if (broadcast_urls) {
    ret = ichabod_attach_rtmp(ichabod_bin, broadcast_urls);
  }

  ichabod_bin_start(ichabod_bin);
  
  g_slist_free(broadcast_urls);
  g_slist_free(output_paths);

  return 0;
}
