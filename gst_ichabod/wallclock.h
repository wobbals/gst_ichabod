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

/* Use this clock's calibration to backdate a timestamp coming from an
 external source. The requirement of "always increasing" does not apply here.
 */
GstClockTime gst_wall_clock_adjust_safe(GstClock* clock,
                                        GstClockTime old_internal);

void gst_wall_clock_do_bootleg_calibration(GstClock* clock, GstClock* master);

G_END_DECLS
#endif /* wallclock_h */
