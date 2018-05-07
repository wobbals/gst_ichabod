//
//  horseman.c
//  ichabod
//
//  Created by Charley Robinson on 6/1/17.
//

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <zmq.h>
#include <uv.h>
#include <assert.h>
#include "horseman.h"

#define PUSH_SOCKET_ADDR "ipc:///tmp/ichabod-push"
#define PULL_SOCKET_ADDR "ipc:///tmp/horseman-push"

#define MESSAGE_TYPE_FRAME "frame"
#define MESSAGE_TYPE_OUTPUT "output"

#define OUTPUT_TYPE_FILE "file"
#define OUTPUT_TYPE_RTMP "rtmp"

struct envelope_s {
  uint8_t* sz_data;
  struct envelope_s* next;
};

struct msg_dispatch_s {
  uv_work_t work;
  struct horseman_s* horseman;
  void* data;
  void (*callback_f)(struct horseman_s* horseman, void* data);
  void (*after_callback_f)(void* data);
};

struct horseman_s {
  void* zmq_ctx;
  void* pull_socket;
  void* push_socket;
  char is_interrupted;
  uv_thread_t zmq_thread;
  
  // Atomic integer counts active jobs queued to uv loop
  uv_mutex_t work_lock;
  int64_t work_count;

  void (*on_video_frame)(struct horseman_s* horseman,
                       struct horseman_frame_s* frame, void* p);
  void (*on_output_request)(struct horseman_s* horseman,
                            struct horseman_output_s* output, void* p);
  void* callback_p;

  // Separate runloop for dispatching callbacks.
  uv_loop_t* loop;
  uv_thread_t loop_thread;
  char is_running;
};

static void envelope_free(struct envelope_s* p) {
  while (p) {
    free(p->sz_data);
    p->sz_data = NULL;
    struct envelope_s* q = p;
    p = p->next;
    free(q);
  }
}

static void video_frame_free(void* p) {
  struct horseman_frame_s* frame = (struct horseman_frame_s*)p;
  if (frame->sz_data) {
    free((void*)frame->sz_data);
    frame->sz_data = NULL;
  }
  free(frame);
}

static void output_free(void* p) {
  struct horseman_output_s* output = (struct horseman_output_s*)p;
  if (output->location) {
    free((void*)output->location);
  }
  free(output);
}

static void async_video_frame_callback(struct horseman_s* pthis, void* data) {
  struct horseman_frame_s* frame = (struct horseman_frame_s*)data;
  pthis->on_video_frame(pthis, frame, pthis->callback_p);
}

static void async_output_callback(struct horseman_s* pthis, void* data) {
  struct horseman_output_s* output = (struct horseman_output_s*)data;
  pthis->on_output_request(pthis, output, pthis->callback_p);
}

// Warning: to prevent excess copying, parsers consume and free the envelope
static struct horseman_frame_s* envelope_parse_frame(struct envelope_s** p) {
  assert(*p);
  struct horseman_frame_s* f = (struct horseman_frame_s*)
  calloc(1, sizeof(struct horseman_frame_s));
  if (!strcmp("EOS", (const char*)(*p)->sz_data)) {
    f->eos = 1;
  } else {
    assert((*p)->sz_data);
    f->sz_data = (const char*)(*p)->sz_data;
    // transfer ownership of frame data string
    (*p)->sz_data = NULL;
    assert((*p)->next);
    assert((*p)->next->sz_data);
    f->timestamp = atof((char*)(*p)->next->sz_data);
  }
  envelope_free(*p);
  *p = NULL;
  return f;
}

static struct horseman_output_s* envelope_parse_output(struct envelope_s** p) {
  assert(*p);
  struct horseman_output_s* output = (struct horseman_output_s*)
  calloc(1, sizeof(struct horseman_output_s));
  assert((*p)->sz_data);
  const char* sz_output_type = (const char*)(*p)->sz_data;
  if (!strcmp(OUTPUT_TYPE_FILE, sz_output_type)) {
    output->output_type = horseman_output_type_file;
  } else if (!strcmp(OUTPUT_TYPE_RTMP, sz_output_type)) {
    output->output_type = horseman_output_type_rtmp;
  }
  (*p)->sz_data = NULL;
  assert((*p)->next);
  assert((*p)->next->sz_data);
  output->location = (const char*)(*p)->next->sz_data;
  (*p)->next->sz_data = NULL;
  envelope_free(*p);
  *p = NULL;
  return output;
}

static void horseman_loop_main(void* p) {
  struct horseman_s* pthis = (struct horseman_s*)p;
  int ret = 0;
  while (pthis->is_running && 0 == ret) {
    ret = uv_run(pthis->loop, UV_RUN_DEFAULT);
    // TODO: should this be a poll instead of a busywait?
    usleep(1000);
  }
  printf("horseman: exiting worker loop\n");
}

void decrement_work_count(struct horseman_s* pthis) {
  uv_mutex_lock(&pthis->work_lock);
  pthis->work_count = 0;
  uv_mutex_unlock(&pthis->work_lock);
}

void increment_work_count(struct horseman_s* pthis) {
  uv_mutex_lock(&pthis->work_lock);
  pthis->work_count++;
  uv_mutex_unlock(&pthis->work_lock);
}

int64_t get_work_count(struct horseman_s* pthis) {
  int64_t ret = 0;
  uv_mutex_lock(&pthis->work_lock);
  ret = pthis->work_count;
  uv_mutex_unlock(&pthis->work_lock);
  return ret;
}

static void issue_callback(uv_work_t* work) {
  struct msg_dispatch_s* async_msg = (struct msg_dispatch_s*) work->data;
  struct horseman_s* pthis = async_msg->horseman;
  if (async_msg->callback_f) {
    async_msg->callback_f(pthis, async_msg->data);
  }
}

static void after_issue_callback(uv_work_t* work, int status) {
  struct msg_dispatch_s* msg = (struct msg_dispatch_s*) work->data;
  msg->after_callback_f(msg->data);
  decrement_work_count(msg->horseman);
}

static int receive_message(void* socket, struct envelope_s** msg_p,
                           char* got_message)
{
  struct envelope_s* root_env = (struct envelope_s*)
  calloc(1, sizeof(struct envelope_s));
  struct envelope_s* env_p = root_env;
  int ret;
  while (1) {
    zmq_msg_t message;
    ret = zmq_msg_init (&message);
    ret = zmq_msg_recv (&message, socket, 0);
    if (ret < 0 && EAGAIN == errno) {
      *got_message = 0;
      zmq_msg_close(&message);
      break;
    }
    *got_message = 1;

    // Copy message frame(s) to C string for easier downstream handling (atof)
    uint8_t* sz_msg = (uint8_t*)malloc(zmq_msg_size(&message) + 1);
    memcpy(sz_msg, zmq_msg_data(&message), zmq_msg_size(&message));
    sz_msg[zmq_msg_size(&message)] = '\0';

    if (env_p->sz_data) {
      env_p->next = (struct envelope_s*) calloc(1, sizeof(struct envelope_s));
      env_p = env_p->next;
    }
    env_p->sz_data = (uint8_t*)sz_msg;

    zmq_msg_close(&message);
    if (!zmq_msg_more(&message))
      break;      //  Last message frame
  }

  if (*got_message) {
    *msg_p = root_env;
  } else {
    envelope_free(root_env);
    *msg_p = NULL;
  }

  return 0;
}

static void parse_envelope(struct horseman_s* pthis, struct envelope_s* msg) {
  int ret = -1;
  struct msg_dispatch_s* async_msg = (struct msg_dispatch_s*)
  calloc(1, sizeof(struct msg_dispatch_s));
  async_msg->horseman = pthis;
  async_msg->work.data = async_msg;

  if (!strcmp(MESSAGE_TYPE_FRAME, (char*)msg->sz_data)) {
    struct horseman_frame_s* frame = envelope_parse_frame(&msg->next);
    if (frame->eos) {
      // let the async callback post, but later break the main receiver loop
      pthis->is_interrupted = 1;
    }
    async_msg->data = frame;
    async_msg->callback_f = async_video_frame_callback;
    async_msg->after_callback_f = video_frame_free;
  } else if (!strcmp(MESSAGE_TYPE_OUTPUT, (char*)msg->sz_data)) {
    struct horseman_output_s* output = envelope_parse_output(&msg->next);
    async_msg->data = output;
    async_msg->callback_f = async_output_callback;
    async_msg->after_callback_f = output_free;
  } else {
    free(async_msg);
    async_msg = NULL;
  }

  if (async_msg) {
    increment_work_count(pthis);
    ret = uv_queue_work(pthis->loop, &async_msg->work,
                        issue_callback, after_issue_callback);
  }

  if (async_msg && ret) {
    // job rejected. don't wait for after_msg to fire
    decrement_work_count(pthis);
    // force the free function for this callback, since it won't run async
    async_msg->after_callback_f(async_msg->data);
  }
}

static int process_next_message(struct horseman_s* pthis) {
  char got_message = 0;
  struct envelope_s* msg = NULL;
  // wait for zmq message
  int ret = receive_message(pthis->pull_socket, &msg, &got_message);
  // process message
  if (ret) {
    printf("horseman: trouble in zmq? %d %d\n", ret, errno);
  }
  if (got_message) {
    parse_envelope(pthis, msg);
    envelope_free(msg);
  }
  return ret;
}

static void horseman_zmq_main(void* p) {
  int ret;
  printf("media queue is online %p\n", p);
  struct horseman_s* pthis = (struct horseman_s*)p;
  int t = 10;
  ret = zmq_connect(pthis->pull_socket, PULL_SOCKET_ADDR);
  if (ret) {
    printf("failed to connect to media queue socket. errno %d\n", errno);
    return;
  }
  ret = zmq_setsockopt(pthis->pull_socket, ZMQ_RCVTIMEO, &t, sizeof(int));
  while (!pthis->is_interrupted) {
    ret = process_next_message(pthis);
  }
  zmq_close(pthis->pull_socket);
}

void horseman_load_config(struct horseman_s* pthis,
                             struct horseman_config_s* config)
{
  pthis->on_video_frame = config->on_video_frame;
  pthis->on_output_request = config->on_output_request;
  pthis->callback_p = config->p;
}

int horseman_alloc(struct horseman_s** queue) {
  struct horseman_s* pthis =
  (struct horseman_s*)calloc(1, sizeof(struct horseman_s));
  pthis->zmq_ctx = zmq_ctx_new();
  pthis->pull_socket = zmq_socket(pthis->zmq_ctx, ZMQ_PULL);
  uv_mutex_init(&pthis->work_lock);
  
  pthis->loop = (uv_loop_t*) malloc(sizeof(uv_loop_t));
  uv_loop_init(pthis->loop);

  *queue = pthis;
  return 0;
}

void horseman_free(struct horseman_s* pthis) {
  pthis->is_running = 0;
  uv_stop(pthis->loop);
  free(pthis->loop);
  uv_thread_join(&pthis->zmq_thread);
  zmq_ctx_destroy(pthis->zmq_ctx);
  uv_mutex_destroy(&pthis->work_lock);
  free(pthis);
}

int horseman_start(struct horseman_s* pthis) {
  pthis->is_interrupted = 0;
  pthis->is_running = 1;
  int ret = uv_thread_create(&pthis->zmq_thread, horseman_zmq_main, pthis);
  int get = uv_thread_create(&pthis->loop_thread, horseman_loop_main, pthis);
  return ret | get;
}

int horseman_stop(struct horseman_s* pthis) {
  int ret;
  if (!pthis->is_running) {
    return -1;
  }
  pthis->is_interrupted = 1;
  pthis->is_running = 0;
  int drain_count = 0;
  // drain the work queue before calling uv_loop_close.
  // don't wait for more than a second.
  // _loop_close will crash if there are executing jobs, but at this point
  // there's not much we can do about it
  while (get_work_count(pthis) > 0 && drain_count < 1000) {
    usleep(1000);
  }
  do {
      ret = uv_loop_close(pthis->loop);
  } while (UV_EBUSY == ret);
  pthis->loop = NULL;
  ret = uv_thread_join(&pthis->zmq_thread);
  int get = uv_thread_join(&pthis->loop_thread);
  return ret | get;
}
