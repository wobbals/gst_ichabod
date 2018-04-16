//
//  broadcast_sink.h
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//

#ifndef broadcast_sink_h
#define broadcast_sink_h

#include "ichabod_bin.h"
#include <gmodule.h>

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, GSList *broadcast_urls);
int ichabod_attach_file(struct ichabod_bin_s* bin, GSList* paths);

#endif /* broadcast_sink_h */
