//
//  screencast_src.h
//  gst_ichabod
//
//  Created by Charley Robinson on 4/13/18.
//

#ifndef screencast_src_h
#define screencast_src_h

#include <stdint.h>
#include <gst/gst.h>

/*
 * Implements the same source element as GstHorsemanSrc, but as an appsrc
 * rather than a full-fledged plugin element.
 */

struct screencast_src_s;

void screencast_src_alloc(struct screencast_src_s** screencast_src_out);
void screencast_src_free(struct screencast_src_s* screencast_src);

void screencast_src_push_frame(struct screencast_src_s* screencast_src,
                               uint64_t timestamp, const char* frame_base64);
void screencast_src_send_eos(struct screencast_src_s* screencast_src);
GstElement* screencast_src_get_element(struct screencast_src_s* screencast_src);

#endif /* screencast_src_h */
