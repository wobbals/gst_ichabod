//
//  ichabod_bin.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/12/17.
//  Copyright © 2017 Charley Robinson. All rights reserved.
//

#include <stdlib.h>
#include <gst/gst.h>
#include "ichabod_bin.h"

struct ichabod_bin_s {
  GstElement* pipeline;

  /* base media - audio */
  GstElement* asource;
  GstElement* avalve;
  GstElement* aqueue;
  GstElement* aconv;
  GstElement* aenc;

  /* base media - video */
  GstElement* vsource;
  GstElement* vqueue;
  GstElement* imgdec;
  GstElement* fps;
  GstElement* venc;

  /* output chain */
  GstElement* video_tee;
  GstElement* audio_tee;
};

void ichabod_bin_alloc(struct ichabod_bin_s** ichabod_bin_out) {
  struct ichabod_bin_s* pthis = (struct ichabod_bin_s*)
  calloc(1, sizeof(struct ichabod_bin_s));

  *ichabod_bin_out = pthis;
}
void ichabod_bin_free(struct ichabod_bin_s* ichabod_bin);

int ichabod_bin_start(struct ichabod_bin_s* ichabod_bin);
int ichabod_bin_stop(struct ichabod_bin_s* ichabod_bin);

int ichabod_bin_attach_mux_sink
(struct ichabod_bin_s* ichabod_bin, GstElement* mux);

