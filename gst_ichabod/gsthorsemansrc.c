
#include <gst/gst.h>
#include "gsthorsemansrc.h"

/* GObject */
static void gst_horsemansrc_get_property
(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_set_property
(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_horsemansrc_finalize(GObject *object);

/* GstBaseSrc */
static gboolean gst_horsemansrc_start (GstBaseSrc *src);
static gboolean gst_horsemansrc_stop (GstBaseSrc *src);

/* GstPushSrc */
static GstFlowReturn
gst_horsemansrc_create (GstPushSrc * src, GstBuffer ** buf);

/* GstElement */
static GstStateChangeReturn gst_horsemansrc_change_state
(GstElement * element, GstStateChange transition);

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
   gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                         gst_caps_new_empty_simple("image/jpeg")));
  
  basesrc_class->start = gst_horsemansrc_start;
  basesrc_class->stop = gst_horsemansrc_stop;
  
  pushsrc_class->create = gst_horsemansrc_create;
  
  gst_element_class_set_static_metadata
  (element_class,
   "horseman video source",
   "Source/Image",
   "horseman video source",
   "charley");
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
  return ret;
}

#pragma mark - Base Source Class Methods
static gboolean gst_horsemansrc_start (GstBaseSrc *src) {
  g_print("ghorse: start\n");
  return TRUE;
}

static gboolean gst_horsemansrc_stop (GstBaseSrc *src) {
  g_print("ghorse: stop\n");
  return TRUE;
}

#pragma mark - Push Source Class Methods
static GstFlowReturn
gst_horsemansrc_create (GstPushSrc * src, GstBuffer ** buf)
{
  g_print("ghorse: pushsrc.create\n");
  return GST_FLOW_OK;
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

GST_PLUGIN_DEFINE (
 GST_VERSION_MAJOR,
 GST_VERSION_MINOR,
 horsemansrc,
 "Template plugin",
 horsemansrc_init,
 "0.0.1",
 "LGPL",
 "horseman",
 "https://wobbals.github.io/horseman/"
)

