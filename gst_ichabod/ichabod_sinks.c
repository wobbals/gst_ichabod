//
//  ichabod_sinks.c
//  gst_ichabod
//
//  Created by Charley Robinson on 12/28/17.
//
#include <stdlib.h>
#include <assert.h>
#include <gst/gst.h>
#include <glib.h>
#include "ichabod_sinks.h"

struct gst_container_s {
  char* sink_factory_name;
  char* mux_factory_name;
  struct ichabod_bin_s* bin;
  GHashTable* sink_hash;
  GHashTable* mux_hash;
  char *audio_request_pad;
  char *video_request_pad;
};

void attach_g_object(gpointer key, gpointer value, gpointer object) {
  gboolean state = FALSE;
  if (!g_strcmp0("1", (char *)value)) {
    state = TRUE;
  }
  g_object_set(G_OBJECT(object), key, state, NULL);
}

void attach_sink(const char *location, struct gst_container_s* container) {
  // Creates an unique factory name
  gchar *mux_name = g_strconcat(container->mux_factory_name, "_", location, NULL);
  gchar *sink_name = g_strconcat(container->sink_factory_name, "_", location, NULL);

  // Prepare mux element
  GstElement* mux = gst_element_factory_make(container->mux_factory_name, mux_name);
  GstElement* sink = gst_element_factory_make(container->sink_factory_name, sink_name);

  g_free(mux_name);
  g_free(sink_name);

  g_object_set(G_OBJECT(sink), "location", location, NULL);
  if (container->sink_hash != NULL) {
    g_hash_table_foreach(container->sink_hash, attach_g_object, sink);
  }
  if (container->mux_hash != NULL)  {
    g_hash_table_foreach(container->mux_hash, attach_g_object, mux);
  }

  int ret = ichabod_bin_add_element(container->bin, mux);
  ret = ichabod_bin_add_element(container->bin, sink);
  gboolean result = gst_element_link(mux, sink);

  GstPad* v_mux_sink = gst_element_get_request_pad(mux, container->video_request_pad);
  GstPad* a_mux_sink = gst_element_get_request_pad(mux, container->audio_request_pad);
  ret = ichabod_bin_attach_mux_sink_pad(container->bin, a_mux_sink, v_mux_sink);
}

static void free_a_hash_table_entry (gpointer key, gpointer value, gpointer user_data) {
  g_free (key);
  g_free (value);
}

int ichabod_attach_rtmp(struct ichabod_bin_s* bin, GSList *broadcast_urls) {
  GHashTable* sink_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  GHashTable* mux_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  // don't sync on sink. sink should not sync.
  g_hash_table_insert(sink_hash, g_strdup("sync"), g_strdup("1"));
  g_hash_table_insert(mux_hash, g_strdup("streamable"), g_strdup("1"));

  // Prepare rmpt container, which needs to be passed into sink loop function
  struct gst_container_s *container = malloc( sizeof( struct gst_container_s ) );
  container->bin = bin;
  container->sink_factory_name = "rtmpsink";
  container->mux_factory_name = "flvmux";
  container->audio_request_pad = "audio";
  container->video_request_pad = "video";
  container->sink_hash = sink_hash;
  container->mux_hash = mux_hash;

  // Go through each element of broadcast list to append bin struct
  g_slist_foreach(broadcast_urls, (GFunc)attach_sink, container);

  free(container);
  g_hash_table_foreach(mux_hash, free_a_hash_table_entry, NULL);
  g_hash_table_foreach(sink_hash, free_a_hash_table_entry, NULL);

  return 1;
}

int ichabod_attach_file(struct ichabod_bin_s* bin, GSList* paths) {
  GHashTable* mux_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  // don't sync on sink. sink should not sync.
  g_hash_table_insert(mux_hash, g_strdup("faststart"), g_strdup("1"));

  // Prepare file container, which needs to be passed into sink loop function
  struct gst_container_s *container = malloc( sizeof( struct gst_container_s ) );
  container->bin = bin;
  container->sink_factory_name = "filesink";
  container->mux_factory_name = "mp4mux";
  container->audio_request_pad = "audio_%u";
  container->video_request_pad = "video_%u";
  container->mux_hash = mux_hash;
  container->sink_hash = NULL;

  // Go through each element of broadcast list to append bin struct
  g_slist_foreach(paths, (GFunc)attach_sink, container);

  free(container);
  g_hash_table_foreach(mux_hash, free_a_hash_table_entry, NULL);

  return 1;
}
