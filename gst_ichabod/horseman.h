//
//  horseman.h
//  ichabod
//
//  Created by Charley Robinson on 6/1/17.
//

#ifndef horseman_h
#define horseman_h

/**
 * Message broker between this process and the horseman, via ZMQ.
 */

struct horseman_s;

struct horseman_frame_s {
  const char* sz_data;
  double timestamp;
  char eos;
};

struct horseman_config_s {
  void (*on_video_frame)(struct horseman_s* queue,
                         struct horseman_frame_s* frame,
                         void* p);
  void* p;
};

int horseman_alloc(struct horseman_s** queue);
void horseman_load_config(struct horseman_s* queue,
                          struct horseman_config_s* config);
void horseman_free(struct horseman_s* queue);

int horseman_start(struct horseman_s* queue);
int horseman_stop(struct horseman_s* queue);

#endif /* horseman_h */
