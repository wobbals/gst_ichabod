//
//  broadcast_sink.h
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//

#ifndef broadcast_sink_h
#define broadcast_sink_h

#include "ichabod_bin.h"

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, const char* broadcast_url);
int ichabod_attach_file(struct ichabod_bin_s* bin, const char* path);
int ichabod_attach_rtp(struct ichabod_bin_s* bin,
                       struct rtp_relay_config_s* rtp_config);

#endif /* broadcast_sink_h */
