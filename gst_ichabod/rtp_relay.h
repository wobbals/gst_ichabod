//
//  rtp_relay.h
//  gst_ichabod
//
//  Created by Charley Robinson on 4/5/18.
//

#ifndef rtp_relay_h
#define rtp_relay_h

#include <gst/gst.h>

struct rtp_relay_s;

struct rtp_relay_config_s {
  char send_enabled;
  char recv_enabled;
  
  char* audio_send_rtp_host;
  int audio_send_rtp_port;
  int audio_recv_rtp_port;
  int audio_send_rtcp_port;
  int audio_recv_rtcp_port;
  unsigned long audio_ssrc;
  char audio_pt;
  char* video_send_rtp_host;
  int video_send_rtp_port;
  int video_recv_rtp_port;
  int video_send_rtcp_port;
  int video_recv_rtcp_port;
  unsigned long video_ssrc;
  char video_pt;
};

void rtp_relay_alloc(struct rtp_relay_s** rtp_relay_out);
void rtp_relay_free(struct rtp_relay_s* rtp_relay);
int rtp_relay_config(struct rtp_relay_s* rtp_relay, struct rtp_relay_config_s*);

GstBin* rtp_relay_get_bin(struct rtp_relay_s* rtp_relay);

int rtp_relay_set_send_video_src(struct rtp_relay_s* rtp_relay,
                                  GstPad* src, GstCaps* caps);
int rtp_relay_set_send_audio_src(struct rtp_relay_s* rtp_relay,
                                  GstPad* src, GstCaps* caps);
int rtp_relay_set_recv_video_src(struct rtp_relay_s* rtp_relay,
                                 GstPad* src, GstCaps* caps);
int rtp_relay_set_recv_audio_src(struct rtp_relay_s* rtp_relay,
                                 GstPad* src, GstCaps* caps);

#endif /* rtp_recv_h */
