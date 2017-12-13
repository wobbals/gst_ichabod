//
//  wallclock.h
//  gst_ichabod
//
//  Created by Charley Robinson on 12/13/17.
//

#ifndef wallclock_h
#define wallclock_h

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_WALL_CLOCK \
(gst_wall_clock_get_type())

#define GST_IS_WALL_CLOCK(obj) \
(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_WALL_CLOCK))

#define GST_IS_WALL_CLOCK_CLASS(obj) \
(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_WALL_CLOCK))

GType gst_wall_clock_get_type(void);

GstClock* gst_wall_clock_new();

G_END_DECLS
#endif /* wallclock_h */
