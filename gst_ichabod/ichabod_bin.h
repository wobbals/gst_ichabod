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

struct ichabod_bin_s;

void ichabod_bin_alloc(struct ichabod_bin_s** ichabod_bin_out);
void ichabod_bin_free(struct ichabod_bin_s* ichabod_bin);
int ichabod_bin_start(struct ichabod_bin_s* ichabod_bin);
int ichabod_bin_stop(struct ichabod_bin_s* ichabod_bin);

int ichabod_bin_attach_mux_sink
(struct ichabod_bin_s* ichabod_bin, GstElement* mux);

#endif /* ichabod_bin_h */
