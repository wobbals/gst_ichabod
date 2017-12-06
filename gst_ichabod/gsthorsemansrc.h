#ifndef __GST_HORSEMANSRC_H__
#define __GST_HORSEMANSRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include "horseman.h"

G_BEGIN_DECLS

#define GST_TYPE_HORSEMANSRC \
(gst_horsemansrc_get_type())
#define GST_HORSEMANSRC(obj) \
(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HORSEMANSRC,GstHorsemanSrc))
#define GST_HORSEMANSRC_CLASS(klass) \
(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HORSEMANSRC,GstHorsemanSrcClass))
#define GST_IS_HORSEMANSRC(obj) \
(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HORSEMANSRC))
#define GST_IS_HORSEMANSRC_CLASS(klass) \
(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HORSEMANSRC))

typedef struct _GstHorsemanSrc GstHorsemanSrc;
typedef struct _GstHorsemanSrcClass GstHorsemanSrcClass;

struct _GstHorsemanSrc {
  GstPushSrcClass push_src;
  GstPad *srcpad;
  
  struct horseman_s* horseman;
  GQueue* frame_queue;
  GMutex mutex;
  GCond data_ready;
  gboolean flushing;
};

struct _GstHorsemanSrcClass
{
  GstPushSrcClass parent_class;
};


gboolean horsemansrc_init (GstPlugin * horsemansrc);

GType gst_horsemansrc_get_type (void);

G_END_DECLS

#endif /* __GST_HORSEMANSRC_H__ */
