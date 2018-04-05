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
#include "ichabod_sinks.h"

#define AUDIO_PORT_OPT 1000
#define AUDIO_HOST_OPT 1001
#define AUDIO_SSRC_OPT 1002
#define AUDIO_PT_OPT   1003
#define AUDIO_RTCP_PORT_OPT 1004
#define VIDEO_PORT_OPT 1015
#define VIDEO_HOST_OPT 1016
#define VIDEO_SSRC_OPT 1017
#define VIDEO_PT_OPT   1018
#define VIDEO_RTCP_PORT_OPT 1019

int main(int argc, char *argv[])
{
  int c;
  char cwd[1024];
  g_print("%d\n", getpid());
  g_print("%s\n", getcwd(cwd, sizeof(cwd)));

  char* output_path = NULL;
  char* broadcast_url = NULL;
  struct rtp_relay_config_s rtp_opts = { 0 };

  static struct option long_options[] =
  {
    {"output", optional_argument,       0, 'o'},
    {"broadcast", optional_argument,       0, 'b'},
    {"audio_port", optional_argument,       0, AUDIO_PORT_OPT},
    {"audio_host", optional_argument,       0, AUDIO_HOST_OPT},
    {"audio_ssrc", optional_argument,       0, AUDIO_SSRC_OPT},
    {"audio_pt", optional_argument,         0, AUDIO_PT_OPT},
    {"audio_rtcp_port", optional_argument,         0, AUDIO_RTCP_PORT_OPT},
    {"video_port", optional_argument,       0, VIDEO_PORT_OPT},
    {"video_host", optional_argument,       0, VIDEO_HOST_OPT},
    {"video_ssrc", optional_argument,       0, VIDEO_SSRC_OPT},
    {"video_pt", optional_argument,         0, VIDEO_PT_OPT},
    {"video_rtcp_port", optional_argument,         0, VIDEO_RTCP_PORT_OPT},
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
      case AUDIO_PORT_OPT:
        rtp_opts.audio_send_rtp_port = atoi(optarg);
        g_print("rtp_audio_port=%d\n", rtp_opts.audio_send_rtp_port);
        break;
      case AUDIO_HOST_OPT:
        rtp_opts.audio_send_rtp_host = optarg;
        g_print("audio_send_rtp_host=%s\n", rtp_opts.audio_send_rtp_host);
        break;
      case AUDIO_SSRC_OPT:
        rtp_opts.audio_ssrc = atol(optarg);
        g_print("rtp_audio_ssrc=%ld\n", rtp_opts.audio_ssrc);
        break;
      case AUDIO_PT_OPT:
        rtp_opts.audio_pt = atoi(optarg);
        g_print("rtp_audio_pt=%d\n", rtp_opts.audio_pt);
        break;
      case AUDIO_RTCP_PORT_OPT:
        rtp_opts.audio_send_rtcp_port = atoi(optarg);
        g_print("rtp_audio_rtcp_port=%d\n", rtp_opts.audio_send_rtcp_port);
        break;
      case VIDEO_PORT_OPT:
        rtp_opts.video_send_rtp_port = atoi(optarg);
        g_print("rtp_video_port=%d\n", rtp_opts.video_send_rtp_port);
        break;
      case VIDEO_HOST_OPT:
        rtp_opts.video_send_rtp_host = optarg;
        g_print("rtp_video_host=%s\n", rtp_opts.video_send_rtp_host);
        break;
      case VIDEO_SSRC_OPT:
        rtp_opts.video_ssrc = atol(optarg);
        g_print("rtp_video_ssrc=%ld\n", rtp_opts.video_ssrc);
        break;
      case VIDEO_PT_OPT:
        rtp_opts.video_pt = atoi(optarg);
        g_print("rtp_video_pt=%d\n", rtp_opts.video_pt);
        break;
      case VIDEO_RTCP_PORT_OPT:
        rtp_opts.video_send_rtcp_port = atoi(optarg);
        g_print("rtp_video_rtcp_port=%d\n", rtp_opts.video_send_rtcp_port);
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
  int ret = ichabod_attach_file(ichabod_bin, output_path);

  if (broadcast_url) {
    g_print("attach broadcast url %s\n", broadcast_url);
    ret = ichabod_attach_rtmp(ichabod_bin, broadcast_url);
  }

  if (rtp_opts.audio_send_rtp_port &&
      rtp_opts.audio_send_rtp_host &&
      rtp_opts.audio_ssrc &&
      rtp_opts.video_send_rtp_port &&
      rtp_opts.video_send_rtp_host &&
      rtp_opts.video_ssrc &&
      rtp_opts.video_pt &&
      rtp_opts.audio_pt)
  {
    rtp_opts.send_enabled = 1;
    ret = ichabod_attach_rtp(ichabod_bin, &rtp_opts);
  } else {
    g_print("missing/incomplete rtp configuration. skipping rtp output\n");
  }

  ichabod_bin_start(ichabod_bin);
  
  return 0;
}
