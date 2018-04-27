//
//  wallclock.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/13/17.
//

#include "wallclock.h"
#include <time.h>

GST_DEBUG_CATEGORY_STATIC (gst_wall_clock_debug_category);
#define GST_CAT_DEFAULT gst_wall_clock_debug_category

#define GST_WALL_CLOCK(obj) \
(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_WALL_CLOCK, GstWallClock))

#define GST_WALL_CLOCK_CLASS(klass) \
(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_WALL_CLOCK, GstWallClockClass))

typedef struct _GstWallClock GstWallClock;
typedef struct _GstWallClockClass GstWallClockClass;

struct _GstWallClock
{
  GstSystemClock parent;
};

struct _GstWallClockClass
{
  /* we want to have async wait and other goodies implemented for us */
  GstSystemClockClass parent_class;
};

enum
{
  PROP_0,

  PROP_LAST
};

#define gst_wall_clock_parent_class parent_class

#define DEBUG_INIT \
GST_DEBUG_CATEGORY_INIT (gst_wall_clock_debug_category, \
"wallclock", 0, "debug category for gst-wallclock");

G_DEFINE_TYPE_WITH_CODE (GstWallClock, gst_wall_clock,
                         GST_TYPE_SYSTEM_CLOCK, DEBUG_INIT);

static void gst_wall_clock_finalize (GObject * object);
static GstClockTime gst_wall_clock_get_internal_time(GstClock* clock);
static guint64 gst_wall_clock_get_resolution(GstClock* clock);

static void gst_wall_clock_class_init(GstWallClockClass* klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstClockClass *clock_class = GST_CLOCK_CLASS(klass);

  gobject_class->finalize = gst_wall_clock_finalize;

  clock_class->get_internal_time =
  GST_DEBUG_FUNCPTR(gst_wall_clock_get_internal_time);

  clock_class->get_resolution =
  GST_DEBUG_FUNCPTR(gst_wall_clock_get_resolution);
}

static void gst_wall_clock_init(GstWallClock* clock)
{
  GST_OBJECT_FLAG_SET(clock, GST_CLOCK_FLAG_CAN_SET_MASTER);
  //GST_OBJECT_FLAG_SET(clock, GST_CLOCK_FLAG_NEEDS_STARTUP_SYNC);
}

static void gst_wall_clock_finalize (GObject * object)
{

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstClockTime gst_wall_clock_get_internal_time(GstClock* clock)
{
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  GstClockTime epochtime = spec.tv_sec;
  epochtime *= 1000000000; // seconds to nanos
  epochtime += spec.tv_nsec; // add remaining nanos
  return epochtime;
}

static guint64 gst_wall_clock_get_resolution(GstClock* clock)
{
  clockid_t ptype = CLOCK_REALTIME;
  struct timespec ts;
  if (G_UNLIKELY(clock_getres(ptype, &ts))) {
    return GST_CLOCK_TIME_NONE;
  }
  return GST_TIMESPEC_TO_TIME(ts);
}

GstClockTime gst_wall_clock_adjust_safe
(GstClock* clock, GstClockTime internal)
{
  GstClockTime ret, cinternal, cexternal, cnum, cdenom;
  gst_clock_get_calibration(clock, &cinternal, &cexternal, &cnum, &cdenom);
  ret = gst_clock_adjust_with_calibration
  (clock, internal, cinternal, cexternal, cnum, cdenom);
  return ret;
}

GstClock* gst_wall_clock_new()
{
  return g_object_new(GST_TYPE_WALL_CLOCK,
                      "clock-type", GST_CLOCK_TYPE_REALTIME,
                      NULL);
}

