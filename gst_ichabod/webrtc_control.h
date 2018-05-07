//
//  webrtc_control.h
//  gst_ichabod
//
//  Created by Charley Robinson on 3/28/18.
//  Copyright © 2018 Charley Robinson. All rights reserved.
//

#ifndef webrtc_control_h
#define webrtc_control_h

struct webrtc_control_s;

struct webrtc_control_config_s {
  void (*on_create_offer)(struct webrtc_control_s* webrtc_control, void* p);
  void (*on_remote_answer)(struct webrtc_control_s* webrtc_control,
                           const char* remote_answer,
                           void* p);
  void (*on_remote_candidate)(struct webrtc_control_s* webrtc_control,
                              int8_t m_line_index, const char* candidate,
                              void* p);
  void* p;
};

void webrtc_control_alloc(struct webrtc_control_s** webrtc_control_out);
void webrtc_control_free(struct webrtc_control_s* webrtc_control);
void webrtc_control_config(struct webrtc_control_s* webrtc_control,
                           struct webrtc_control_config_s* config);

int webrtc_control_start(struct webrtc_control_s* webrtc_control);
int webrtc_control_stop(struct webrtc_control_s* webrtc_control);

int webrtc_control_send_offer(struct webrtc_control_s* webrtc_control,
                              const char* offer);
int webrtc_control_send_ice_candidate(struct webrtc_control_s* webrtc_control,
                                      int8_t m_line_index,
                                      const char* candidate);

#endif /* webrtc_control_h */
