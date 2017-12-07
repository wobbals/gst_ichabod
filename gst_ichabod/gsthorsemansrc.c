
#include <gst/gst.h>
#include "gsthorsemansrc.h"
#include "base64.h"

enum {
  /* signals */
  SIGNAL_FIRST_FRAME,
  LAST_SIGNAL
};

static guint gst_horsemansrc_signals[LAST_SIGNAL] = { 0 };

/* GObject */
static void gst_horsemansrc_get_property
(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_set_property
(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_finalize(GObject *object);

/* GstBaseSrc */
static gboolean gst_horsemansrc_start (GstBaseSrc *src);
static gboolean gst_horsemansrc_stop (GstBaseSrc *src);
static gboolean gst_horsemansrc_unlock (GstBaseSrc * pthis);
static gboolean gst_horsemansrc_unlock_stop (GstBaseSrc * pthis);

/* GstPushSrc */
static GstFlowReturn
gst_horsemansrc_create (GstPushSrc * src, GstBuffer ** buf);

/* GstElement */
static GstStateChangeReturn gst_horsemansrc_change_state
(GstElement * element, GstStateChange transition);

/* Horseman */
void on_horseman_cb(struct horseman_s* queue,
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
  
  gst_element_class_add_pad_template
  (element_class,
   gst_pad_template_new
   ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    gst_caps_new_empty_simple("image/jpeg")));
  
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
  
  pushsrc_class->create = gst_horsemansrc_create;
  
  gst_horsemansrc_signals[SIGNAL_FIRST_FRAME] =
  g_signal_new ("first-frame", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST, 0,
                NULL, NULL, NULL, G_TYPE_NONE, 0, NULL);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_horsemansrc_init (GstHorsemanSrc* pthis)
{
  gst_base_src_set_format (GST_BASE_SRC (pthis), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (pthis), TRUE);
  
  struct horseman_config_s hconf;
  horseman_alloc(&pthis->horseman);
  hconf.p = pthis;
  hconf.on_video_msg = on_horseman_cb;
  horseman_load_config(pthis->horseman, &hconf);
  
  pthis->frame_queue = g_queue_new();
  g_mutex_init(&pthis->mutex);
  g_cond_init(&pthis->data_ready);
}

// magic macros autodiscover the two functions named above this one :-|
#define gst_horsemansrc_parent_class parent_class
G_DEFINE_TYPE (GstHorsemanSrc, gst_horsemansrc, GST_TYPE_PUSH_SRC);

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
(GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  g_print("ghorse: state: %d\n", transition);
  
  // pass state change to parent element
  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  
  return ret;
}

#pragma mark - Base Source Class Methods
static gboolean gst_horsemansrc_start(GstBaseSrc* base) {
  g_print("ghorse: start\n");
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(base);
  pthis->is_eos = FALSE;
  return (0 == horseman_start(pthis->horseman));
}

static gboolean gst_horsemansrc_stop(GstBaseSrc* base) {
  g_print("ghorse: stop\n");
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(base);
  return 0 == horseman_stop(pthis->horseman);
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

#pragma mark - Push Source Class Methods
static GstFlowReturn gst_horsemansrc_create(GstPushSrc* src, GstBuffer ** buf)
{
  g_print("ghorse: pushsrc.create\n");
  GstFlowReturn ret;
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(src);
  g_mutex_lock(&pthis->mutex);
  while (g_queue_is_empty(pthis->frame_queue) &&
         !pthis->flushing &&
         !pthis->is_eos)
  {
    g_print("ghorse: still waiting\n");
    g_cond_wait(&pthis->data_ready, &pthis->mutex);
  }
  GstBuffer* head = g_queue_pop_head(pthis->frame_queue);
  ret = head ? GST_FLOW_OK : GST_FLOW_ERROR;
  *buf = head;
  if (pthis->flushing) {
    ret = GST_FLOW_FLUSHING;
  }
  if (pthis->is_eos) {
    ret = GST_FLOW_EOS;
  }
  gboolean signal_first_frame = FALSE;
  if (!pthis->first_frame_received && GST_FLOW_OK == ret) {
    signal_first_frame = TRUE;
    pthis->first_frame_received = TRUE;
  }
  g_mutex_unlock(&pthis->mutex);
  if (signal_first_frame) {
    g_signal_emit(pthis, gst_horsemansrc_signals[SIGNAL_FIRST_FRAME], NULL);
  }
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
  GstBuffer* buf = gst_buffer_new_wrapped(b_img, b_length);
  return buf;
}

void on_horseman_cb(struct horseman_s* queue,
                    struct horseman_msg_s* msg,
                    void* p)
{
  GstHorsemanSrc* pthis = GST_HORSEMANSRC(p);
  GstElement* element = GST_ELEMENT(p);
  GstBuffer* buf = NULL;
  
  if (!msg->eos) {
    buf = wrap_message(msg);
    // TODO: How does this clock mechanism work?
    //GstClockTime base_time = gst_element_get_base_time(element);
    //buf->pts = msg->timestamp;
    buf->pts = gst_clock_get_time(gst_element_get_clock(element));
    buf->dts = buf->pts;
  }
  
  g_mutex_lock(&pthis->mutex);
  if (buf) {
    g_queue_push_tail(pthis->frame_queue, buf);
    //size_t len = g_queue_get_length(pthis->frame_queue);
    g_print("queue push pts %d\n", buf->pts);
  } else if (msg->eos) {
    pthis->is_eos = TRUE;
  }
  g_cond_broadcast(&pthis->data_ready);
  g_mutex_unlock(&pthis->mutex);
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
