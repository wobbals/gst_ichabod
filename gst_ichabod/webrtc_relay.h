//
//  webrtc_relay.h
//  gst_ichabod
//
//  Created by Charley Robinson on 4/9/18.
//

#ifndef webrtc_relay_h
#define webrtc_relay_h

#include <gst/gst.h>

struct webrtc_relay_s;

struct webrtc_relay_config_s {
  // does this need to be async or can we just pass in the sinkpad we want?
  //void (*on_video_rtp_sink)(struct webrtc_relay_s*, GstPad*);

  GstPad* video_rtp_src;
  GstPad* audio_rtp_src;
  GstCaps* video_caps;
  GstCaps* audio_caps;
};

void webrtc_relay_alloc(struct webrtc_relay_s** webrtc_relay_out);
void webrtc_relay_free(struct webrtc_relay_s* webrtc_relay);
void webrtc_relay_config(struct webrtc_relay_s* webrtc_relay,
                         struct webrtc_relay_config_s* config);

GstBin* webrtc_relay_get_bin(struct webrtc_relay_s* webrtc_relay);

#endif /* webrtc_relay_h */
