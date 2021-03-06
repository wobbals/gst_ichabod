//
//  ichabod_bin.h
//  gst_ichabod
//
//  Created by Charley Robinson on 12/12/17.
//

/**
 * Base bin for ichabod media pipeline. Will take multiple attachments for
 * output sinks (namely multiplexer and file output).
 */

#ifndef ichabod_bin_h
#define ichabod_bin_h

#include <gst/gst.h>

#include "rtp_relay.h"

struct ichabod_bin_s;

void ichabod_bin_alloc(struct ichabod_bin_s** ichabod_bin_out);
void ichabod_bin_free(struct ichabod_bin_s* ichabod_bin);
int ichabod_bin_start(struct ichabod_bin_s* ichabod_bin);
int ichabod_bin_stop(struct ichabod_bin_s* ichabod_bin);

int ichabod_bin_add_element(struct ichabod_bin_s* bin, GstElement* element);
int ichabod_bin_attach_mux_sink_pad
(struct ichabod_bin_s* bin, GstPad* audio_sink, GstPad* video_sink);

GstPad* ichabod_bin_create_audio_src(struct ichabod_bin_s* bin, GstCaps* caps);
GstPad* ichabod_bin_create_video_src(struct ichabod_bin_s* bin, GstCaps* caps);

void ichabod_bin_set_rtp_relay(struct ichabod_bin_s* ichabod_bin,
                               struct rtp_relay_s* rtp_relay);

#endif /* ichabod_bin_h */
