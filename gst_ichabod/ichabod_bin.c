//
//  ichabod_bin.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/12/17.
//  Copyright © 2017 Charley Robinson. All rights reserved.
//

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
};
