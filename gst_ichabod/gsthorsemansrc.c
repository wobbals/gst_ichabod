//
//  gsthorsemansrc.c
//  gst_ichabod
//
//  Created by Charley Robinson on 9/10/17.
//

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <gst/gst.h>
#include "gsthorsemansrc.h"
#include "wallclock.h"
#include "base64.h"

enum {
  /* signals */
  LAST_SIGNAL
};

static guint gst_horsemansrc_signals[LAST_SIGNAL] = { };

/* GObject */
static void gst_horsemansrc_get_property
(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_set_property
(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_finalize(GObject *object);

/* GstElement */
static GstStateChangeReturn gst_horsemansrc_change_state
(GstElement * element, GstStateChange transition);
static GstClock* gst_horsemansrc_provide_clock(GstElement* element);
static gboolean gst_horsemansrc_set_clock(GstElement* element, GstClock* clock);

/* GstBaseSrc */
static gboolean gst_horsemansrc_start(GstBaseSrc* src);
static gboolean gst_horsemansrc_stop(GstBaseSrc* src);
static gboolean gst_horsemansrc_unlock(GstBaseSrc* pthis);
static gboolean gst_horsemansrc_unlock_stop(GstBaseSrc* pthis);
static gboolean gst_horsemansrc_event(GstBaseSrc* src, GstEvent* event);
static gboolean gst_horsemansrc_is_seekable(GstBaseSrc* src);
static gboolean gst_horsemansrc_query(GstBaseSrc* src, GstQuery* query);

/* GstPushSrc */
static GstFlowReturn
gst_horsemansrc_create (GstPushSrc * src, GstBuffer ** buf);

// magic macros autodiscover the functions named above this one :-|
#define gst_horsemansrc_parent_class parent_class
G_DEFINE_TYPE (GstHorsemanSrc, gst_horsemansrc, GST_TYPE_PUSH_SRC);

/* Horseman */
static void on_horseman_cb(struct horseman_s* queue,
                    struct horseman_msg_s* msg,
                    void* p);

#pragma mark - Plugin Definitions

/* initialize the horsemansrc's class */
static void
gst_horsemansrc_class_init (GstHorsemanSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;
  
  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesrc_class = GST_BASE_SRC_CLASS (klass);
  pushsrc_class = GST_PUSH_SRC_CLASS (klass);
  
  gobject_class->finalize = (GObjectFinalizeFunc) gst_horsemansrc_finalize;
  gobject_class->set_property = gst_horsemansrc_set_property;
  gobject_class->get_property = gst_horsemansrc_get_property;
  
  element_class->change_state = gst_horsemansrc_change_state;
  element_class->provide_clock = gst_horsemansrc_provide_clock;
  element_class->set_clock = gst_horsemansrc_set_clock;

  gst_element_class_add_pad_template
  (element_class,
   gst_pad_template_new
   ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    gst_caps_new_simple("image/jpeg",
                        "framerate", GST_TYPE_FRACTION, 0, 1,
                        NULL)));
  
  gst_element_class_set_static_metadata
  (element_class,
   "horseman video source",
   "Source/Image",
   "horseman video source",
   "charley");
  
  basesrc_class->start = gst_horsemansrc_start;
  basesrc_class->stop = gst_horsemansrc_stop;
  basesrc_class->unlock = gst_horsemansrc_unlock;
  basesrc_class->unlock_stop = gst_horsemansrc_unlock_stop;
  basesrc_class->event = gst_horsemansrc_event;
  basesrc_class->is_seekable = gst_horsemansrc_is_seekable;
  basesrc_class->query = gst_horsemansrc_query;

  pushsrc_class->create = gst_horsemansrc_create;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_horsemansrc_init (GstHorsemanSrc* pthis)
{
  GST_OBJECT_FLAG_SET(pthis, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET(pthis, GST_ELEMENT_FLAG_SOURCE);

  gst_base_src_set_format(GST_BASE_SRC (pthis), GST_FORMAT_TIME);
  gst_base_src_set_live(GST_BASE_SRC (pthis), TRUE);
  gst_base_src_set_async(GST_BASE_SRC(pthis), TRUE);

  pthis->walltime_clock = gst_wall_clock_new();
  pthis->horseman_started = FALSE;
  pthis->master_clock_sync = TRUE;
  pthis->start_ack = FALSE;

  struct horseman_config_s hconf;
  horseman_alloc(&pthis->horseman);
  hconf.p = pthis;
  hconf.on_video_msg = on_horseman_cb;
  horseman_load_config(pthis->horseman, &hconf);
  
  pthis->frame_queue = g_queue_new();
  g_mutex_init(&pthis->mutex);
  g_cond_init(&pthis->data_ready);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean horsemansrc_init (GstPlugin * horsemansrc)
{ 
  return gst_element_register (horsemansrc, "horsemansrc", GST_RANK_NONE,
      GST_TYPE_HORSEMANSRC);
}

#pragma mark - GObject Class Methods

static void gst_horsemansrc_set_property
(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  g_print("ghorse: set prop_id %d\n", prop_id);
}

static void gst_horsemansrc_get_property
(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  g_print("ghorse: get prop_id %d\n", prop_id);
}

static void gst_horsemansrc_finalize(GObject *object) {
  g_print("ghorse: finalize called\n");
}

#pragma mark - GstElement Class Methods

static GstStateChangeReturn gst_horsemansrc_change_state
(GstElement* element, GstStateChange transition)
{
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!pthis->horseman_started) {
        pthis->horseman_started = TRUE;
        ret = !horseman_start(pthis->horseman) ?
        GST_STATE_CHANGE_SUCCESS : GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (pthis->horseman_started) {
        pthis->horseman_started = FALSE;
        horseman_stop(pthis->horseman);
      }
    default:
      break;
  }
  // pass state change to parent element. this must *always* be called, even
  // if subclass handles the signal (as the switch above this comment).
  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  GstState state;
  GstState pending;
  GstStateChangeReturn current_ret =
  gst_element_get_state(element, &state, &pending, 0);
  g_print("ghorse: state change: %d (ret=%d)\n", transition, ret);
  g_print("current ret=%d state=%s pend=%s\n", current_ret,
          gst_element_state_get_name(state),
          gst_element_state_get_name(pending));
  return ret;
}

static GstClock* gst_horsemansrc_provide_clock(GstElement* element) {
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(element);
  return gst_object_ref(pthis->walltime_clock);
}

static gboolean master_synchronize_cb(GstClock* clock,
                                      GstClockTime time,
                                      GstClockID id,
                                      gpointer p_user)
{
  g_print("ghorse: master calibration callback\n");
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(p_user);

  // this represents how much time we expect for the internal clock to sync.
  gst_clock_id_unref(id);

  g_mutex_lock(&pthis->mutex);
  pthis->master_clock_sync = TRUE;
  g_mutex_unlock(&pthis->mutex);

  return TRUE;
}

static gboolean gst_horsemansrc_set_clock(GstElement* element, GstClock* clock)
{
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(element);
  gboolean result;
  if (clock && clock != pthis->walltime_clock) {
    GstClock* master = clock;
    GstClock* slave = pthis->walltime_clock;
    result = gst_clock_set_master(slave, master);
    g_print("slaving clock %s to master %s (ret=%d)\n",
            gst_object_get_name(GST_OBJECT(slave)),
            gst_object_get_name(GST_OBJECT(master)),
            result);

    // one-shot calibration bootleg to get things started immediately
    GstClockTime internal = gst_clock_get_internal_time(slave);
    GstClockTime external = gst_clock_get_time(master);
    // just assume the clocks run at the same speed for now (should be close)
    gst_clock_set_calibration(slave, internal, external, 1, 1);

    GstClockTime timeout = gst_clock_get_timeout(slave);
    gint window_size;
    g_object_get(G_OBJECT(slave), "window-size", &window_size, NULL);
    gint window_threshold;
    g_object_get(G_OBJECT(slave), "window-threshold", &window_threshold, NULL);
    g_print("master clock is_sync=%d\n", gst_clock_is_synced(clock));
    g_print("slave clock timeout=%ld\n", timeout);
    g_print("slave clock window_size=%d\n", window_size);
    g_print("slave clock window_thresh=%d\n", window_threshold);
    timeout *= window_size;
    GstClockID await_id =
    gst_clock_new_single_shot_id(master, gst_clock_get_time(master) + timeout);
    GstClockReturn ret =
    gst_clock_id_wait_async(await_id, master_synchronize_cb, pthis, NULL);
    g_print("slave clock async wait for calibration ret=%d\n", ret);
    g_mutex_lock(&pthis->mutex);
    pthis->master_clock_sync = FALSE;
    if (GST_CLOCK_OK != ret) {
      gst_clock_id_unref(await_id);
    }
    g_mutex_unlock(&pthis->mutex);
  }
  result = GST_ELEMENT_CLASS(parent_class)->set_clock(element, clock);
  return result;
}

#pragma mark - Base Source Class Methods
static gboolean gst_horsemansrc_start(GstBaseSrc* base) {
  g_print("ghorse: start\n");
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(base);
  pthis->is_eos = FALSE;
  GstFlowReturn ret = gst_base_src_start_wait(base);
  return TRUE;
}

static gboolean gst_horsemansrc_stop(GstBaseSrc* base) {
  g_print("ghorse: stop\n");
  return TRUE;
}

static gboolean gst_horsemansrc_unlock(GstBaseSrc* base)
{
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(base);
  g_print("ghorse: unlock\n");
  g_mutex_lock(&pthis->mutex);
  pthis->flushing = TRUE;
  g_cond_broadcast(&pthis->data_ready);
  g_mutex_unlock(&pthis->mutex);
  return TRUE;
}

static gboolean gst_horsemansrc_unlock_stop(GstBaseSrc* base)
{
  GstHorsemanSrc *pthis = GST_HORSEMANSRC (base);
  g_print("ghorse: unlock_stop\n");
  g_mutex_lock(&pthis->mutex);
  pthis->flushing = FALSE;
  g_cond_broadcast(&pthis->data_ready);
  g_mutex_unlock(&pthis->mutex);
  return TRUE;
}

static gboolean gst_horsemansrc_event (GstBaseSrc * src, GstEvent * event) {
  g_print("ghorse event: %s\n", gst_event_type_get_name(event->type));
  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_LATENCY:
    {
      GstClockTime latency = GST_CLOCK_TIME_NONE;
      gst_event_parse_latency(event, &latency);
      g_print("latency is %ld\n", latency);
      break;
    }
    default:
      break;
  }
  return GST_BASE_SRC_CLASS (parent_class)->event (src, event);
}

static gboolean gst_horsemansrc_is_seekable(GstBaseSrc* src) {
  return FALSE;
}

static gboolean gst_horsemansrc_query(GstBaseSrc* src, GstQuery* query)
{
  g_print("ghorse: query %s\n", GST_QUERY_TYPE_NAME(query));
  gboolean result;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      gst_query_parse_latency(query, &live, &min, &max);
      //min += (GST_SECOND / 30);
      if (GST_CLOCK_TIME_NONE != max) {
        max += (GST_SECOND / 2);
      }
      min = GST_SECOND / 2;
      gst_query_set_latency(query, TRUE, min, max);
      result = TRUE;
      break;
    }
    default:
    {
      result = GST_BASE_SRC_CLASS(parent_class)->query(src, query);
      break;
    }
  }
  return result;
}

#pragma mark - Push Source Class Methods

static GstFlowReturn gst_horsemansrc_create(GstPushSrc* src, GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(src);
  g_print("ghorse: pushsrc.create\n");
  g_mutex_lock(&pthis->mutex);
  while (g_queue_get_length(pthis->frame_queue) < 2 &&
         !pthis->flushing &&
         !pthis->is_eos)
  {
    g_print("ghorse: still waiting\n");
    g_cond_wait(&pthis->data_ready, &pthis->mutex);
  }
  GstBuffer* head = g_queue_pop_head(pthis->frame_queue);
  GstBuffer* next = g_queue_peek_head(pthis->frame_queue);
  if (head && next) {
    ret = GST_FLOW_OK;
    head->duration = next->pts - head->pts;
  } else {
    ret = GST_FLOW_ERROR;
  }
  *buf = head;
  if (pthis->flushing) {
    ret = GST_FLOW_FLUSHING;
  }
  if (pthis->is_eos) {
    ret = GST_FLOW_EOS;
  }
  g_mutex_unlock(&pthis->mutex);

  g_print("ghorse: create ret %d\n", ret);
  return ret;
}

#pragma mark - Horseman

static GstBuffer* wrap_message(struct horseman_msg_s* msg) {
  size_t b_length = 0;
  const uint8_t* b_img =
  base64_decode((const unsigned char*)msg->sz_data,
                strlen(msg->sz_data),
                &b_length);
  GstBuffer* buf = gst_buffer_new_wrapped((gpointer)b_img, b_length);
  return buf;
}

static void on_horseman_cb(struct horseman_s* queue,
                    struct horseman_msg_s* msg,
                    void* p)
{
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(p);
  GstBuffer* buf = NULL;
  gboolean allow_frame = FALSE;

  g_mutex_lock(&pthis->mutex);
  if (msg->eos) {
    pthis->is_eos = TRUE;
    g_cond_broadcast(&pthis->data_ready);
  }
  GstClock* active_clock = gst_element_get_clock(GST_ELEMENT(pthis));
  if (pthis->master_clock_sync && active_clock) {
    pthis->frame_ct++;
    allow_frame = TRUE;
  }
  if (!pthis->start_ack) {
    pthis->start_ack = TRUE;
    gst_base_src_start_complete(GST_BASE_SRC(pthis), GST_FLOW_OK);
  }
  g_mutex_unlock(&pthis->mutex);

  if (active_clock) {
    GstClockTime internal, external, rate_n, rate_d;
    gst_clock_get_calibration(pthis->walltime_clock,
                              &internal, &external, &rate_n, &rate_d);
    g_print("ghorse: wallclock calibration %lu, %lu, %lu / %lu\n",
            internal, external, rate_n, rate_d);

  } else {
    g_print("ghorse: missing active clock\n");
  }

  if (!msg->eos && allow_frame) {
    buf = wrap_message(msg);
    GstClockTime timestamp_nano = msg->timestamp * GST_MSECOND;
    GstClockTime adjusted_pts =
    gst_wall_clock_adjust_safe(pthis->walltime_clock, timestamp_nano);
    GstClockTime current_time = gst_clock_get_time(pthis->walltime_clock);
    g_print("CLOCK DELTA %lu (base_time=%lu)\n",
            current_time - adjusted_pts,
            gst_element_get_base_time(GST_ELEMENT(pthis)));

    buf->pts = adjusted_pts;
    buf->dts = adjusted_pts;
    buf->duration = GST_CLOCK_TIME_NONE;
  }

  if (buf) {
    g_mutex_lock(&pthis->mutex);
    GQueue* queue = pthis->frame_queue;
    g_queue_push_tail(queue, buf);
    size_t len = g_queue_get_length(queue);
    g_cond_broadcast(&pthis->data_ready);
    g_mutex_unlock(&pthis->mutex);
    g_print("queue push pts %lu (from %.00f) n=%ld tot=%llu\n",
            buf->pts, msg->timestamp,
            len, pthis->frame_ct);

    if (pthis->frame_ct == 300) {
      GST_DEBUG_BIN_TO_DOT_FILE(gst_object_get_parent(GST_ELEMENT(pthis)),
                                GST_DEBUG_GRAPH_SHOW_ALL,
                                "pipeline300");
    }
  }
}

#pragma mark GStreamer plugin registration

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "horsemansrc"
#endif

GST_PLUGIN_DEFINE
(
 GST_VERSION_MAJOR,
 GST_VERSION_MINOR,
 horsemansrc,
 "horseman plugin",
 horsemansrc_init,
 "0.0.1",
 "LGPL",
 "horseman",
 "https://wobbals.github.io/horseman/"
 )
