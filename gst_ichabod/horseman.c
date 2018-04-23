//
//  horseman.c
//  ichabod
//
//  Created by Charley Robinson on 6/1/17.
//

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <zmq.h>
#include <uv.h>
#include <assert.h>
#include "horseman.h"

struct horseman_s {
  void* zmq_ctx;
  void* screencast_socket;
  char is_interrupted;
  uv_thread_t zmq_thread;
  
  // Atomic integer counts active jobs queued to uv loop
  uv_mutex_t work_lock;
  int64_t work_count;

  void (*on_video_msg)(struct horseman_s* queue,
                       struct horseman_msg_s* msg, void* p);
  void* callback_p;

  // Separate runloop for dispatching callbacks.
  uv_loop_t* loop;
  uv_thread_t loop_thread;
  char is_running;

  uint64_t start_time;
};

static void horseman_loop_main(void* p) {
  struct horseman_s* pthis = (struct horseman_s*)p;
  int ret = 0;
  while (pthis->is_running && 0 == ret) {
    ret = uv_run(pthis->loop, UV_RUN_DEFAULT);
    // todo: why is this not running on a poll?
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

static void horseman_msg_free(struct horseman_msg_s* msg) {
  if (msg->sz_data) {
    free(msg->sz_data);
  }
  if (msg->sz_sid) {
    free(msg->sz_sid);
  }
  free(msg);
}

static int receive_message(void* socket, struct horseman_msg_s* msg,
                           char* got_message)
{
  int ret;
  if (msg->sz_data) {
    free(msg->sz_data);
  }
  if (msg->sz_sid) {
    free(msg->sz_sid);
  }
  if (msg->eos) {
    msg->eos = 0;
  }
  memset(msg, 0, sizeof(struct horseman_msg_s));
  while (1) {
    zmq_msg_t message;
    ret = zmq_msg_init (&message);
    ret = zmq_msg_recv (&message, socket, 0);
    if (ret < 0 && EAGAIN == errno) {
      *got_message = 0;
      zmq_msg_close(&message);
      return 0;
    }
    *got_message = 1;
    // Process the message frame
    uint8_t* sz_msg = (uint8_t*)malloc(zmq_msg_size(&message) + 1);
    memcpy(sz_msg, zmq_msg_data(&message), zmq_msg_size(&message));
    sz_msg[zmq_msg_size(&message)] = '\0';

    if (!msg->sz_data) {
      msg->sz_data = (char*)sz_msg;
    } else if (!msg->timestamp) {
      msg->timestamp = atof((char*)sz_msg);
      free(sz_msg);
    } else if (!msg->sz_sid) {
      msg->sz_sid = (char*)sz_msg;
    } else {
      printf("unknown extra message part received. freeing.");
      free(sz_msg);
    }
    if (!strcmp("EOS", msg->sz_data)) {
      printf("horseman: received EOS\n");
      free(msg->sz_data);
      msg->sz_data = NULL;
      msg->eos = 1;
      break;
    }
    
    zmq_msg_close (&message);
    if (!zmq_msg_more (&message))
      break;      //  Last message frame
  }
  return 0;
}

struct msg_dispatch_s {
  uv_work_t work;
  struct horseman_msg_s* msg;
  struct horseman_s* horseman;
};

static void dispatch_video_msg(uv_work_t* work) {
  struct msg_dispatch_s* async_msg = (struct msg_dispatch_s*) work->data;
  struct horseman_s* pthis = async_msg->horseman;
  struct horseman_msg_s* msg = async_msg->msg;
  int64_t ts_nano = msg->timestamp * 1000000;
  msg->timestamp = ts_nano;
  if (msg->timestamp >= 0) {
    pthis->on_video_msg(pthis, msg, pthis->callback_p);
  }
}

static void after_video_msg(uv_work_t* work, int status) {
  struct msg_dispatch_s* msg = (struct msg_dispatch_s*) work->data;
  decrement_work_count(msg->horseman);
  horseman_msg_free(msg->msg);
  free(msg);
}

static int receive_screencast(struct horseman_s* pthis, char* got_message) {
  struct horseman_msg_s* msg = calloc(1, sizeof(struct horseman_msg_s));
  // wait for zmq message
  int ret = receive_message(pthis->screencast_socket, msg, got_message);
  // process message
  if (ret) {
    printf("trouble? %d %d\n", ret, errno);
  } else if (*got_message && msg->eos) {
    // let the dispatch_video_msg job post, but break the main receiver loop
    pthis->is_interrupted = 1;
  }
  if (*got_message) {
    printf("received screencast  ts=%f\n",
           msg->timestamp);

    struct msg_dispatch_s* async_msg = (struct msg_dispatch_s*)
    calloc(1, sizeof(struct msg_dispatch_s));
    async_msg->msg = msg;
    async_msg->horseman = pthis;
    async_msg->work.data = async_msg;
    increment_work_count(pthis);
    ret = uv_queue_work(pthis->loop, &async_msg->work,
                        dispatch_video_msg, after_video_msg);
    if (ret) {
      // job rejected. don't wait for after_msg to fire
      decrement_work_count(pthis);
    }
  }
  return ret;
}

static void horseman_zmq_main(void* p) {
  int ret;
  printf("media queue is online %p\n", p);
  struct horseman_s* pthis = (struct horseman_s*)p;
  int t = 10;
  ret = zmq_connect(pthis->screencast_socket, "ipc:///tmp/ichabod-screencast");
  if (ret) {
    printf("failed to connect to media queue socket. errno %d\n", errno);
    return;
  }
  ret = zmq_setsockopt(pthis->screencast_socket, ZMQ_RCVTIMEO, &t, sizeof(int));
  while (!pthis->is_interrupted) {
    char got_screencast = 0;
    ret = receive_screencast(pthis, &got_screencast);
  }
  zmq_close(pthis->screencast_socket);
}

void horseman_load_config(struct horseman_s* pthis,
                             struct horseman_config_s* config)
{
  pthis->on_video_msg = config->on_video_msg;
  pthis->callback_p = config->p;
}

int horseman_alloc(struct horseman_s** queue) {
  struct horseman_s* pthis =
  (struct horseman_s*)calloc(1, sizeof(struct horseman_s));
  pthis->zmq_ctx = zmq_ctx_new();
  pthis->screencast_socket = zmq_socket(pthis->zmq_ctx, ZMQ_PULL);
  uv_mutex_init(&pthis->work_lock);
  
  pthis->loop = (uv_loop_t*) malloc(sizeof(uv_loop_t));
  uv_loop_init(pthis->loop);

  *queue = pthis;
  return 0;
}

void horseman_free(struct horseman_s* pthis) {
  int ret;
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

  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  pthis->start_time = spec.tv_sec;
  pthis->start_time *= 1000000000; // seconds to nanos
  pthis->start_time += spec.tv_nsec; // add remaining nanos

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
  while(get_work_count(pthis) > 0 && drain_count < 1000) {
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
