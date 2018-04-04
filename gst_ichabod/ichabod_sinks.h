//
//  broadcast_sink.h
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//

#ifndef broadcast_sink_h
#define broadcast_sink_h

#include "ichabod_bin.h"

struct rtp_opts_s {
  char* audio_host;
  int audio_port;
  int audio_rtcp_port;
  unsigned long audio_ssrc;
  char audio_pt;
  char* video_host;
  int video_port;
  int video_rtcp_port;
  unsigned long video_ssrc;
  char video_pt;
};

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, const char* broadcast_url);
int ichabod_attach_file(struct ichabod_bin_s* bin, const char* path);
int ichabod_attach_rtp(struct ichabod_bin_s* bin, struct rtp_opts_s* rtp_opts);

#endif /* broadcast_sink_h */
