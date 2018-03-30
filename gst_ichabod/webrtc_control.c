//
//  webrtc_control.c
//  gst_ichabod
//
//  Created by Charley Robinson on 3/28/18.
//  Copyright © 2018 Charley Robinson. All rights reserved.
//

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <zmq.h>
#include <uv.h>

#include "webrtc_control.h"

struct webrtc_control_s {
  void* zmq_ctx;
  void* push_socket;
  void* pull_socket;
  char is_interrupted;
  char is_running;
  uv_thread_t zmq_thread;
  uv_thread_t cb_thread;
  uv_loop_t* cb_loop;

  void (*create_offer_cb)(struct webrtc_control_s* webrtc_control, void* p);
  void (*remote_answer_cb)(struct webrtc_control_s* webrtc_control,
                           const char* remote_answer,
                           void* p);

  void* callback_p;
};

struct ctrl_msg_s {
  char* type;
  char* data;
};

static void clear_msg(struct ctrl_msg_s* msg) {
  if (msg->type) {
    free(msg->type);
    msg->type = NULL;
  }
  if (msg->data) {
    free(msg->data);
    msg->data = NULL;
  }
}

static void cb_loop_main(void* p) {
  struct webrtc_control_s* pthis = (struct webrtc_control_s*)p;
  int ret = 0;
  while (pthis->is_running && 0 == ret) {
    ret = uv_run(pthis->cb_loop, UV_RUN_DEFAULT);
    // todo: why is this not running on a poll?
    usleep(1000);
  }
  printf("webrtc_control: exiting cb loop\n");
}

#define MSG_TYPE_CREATE_OFFER "create_offer"
#define MSG_TYPE_SET_REMOTE_DESCRIPTION "set_remote_description"

static int handle_msg(struct webrtc_control_s* pthis, struct ctrl_msg_s* msg) {
  printf("webrtc_control: handle_msg: type=%s, data=%s\n",
         msg->type, msg->data);
  if (!strcmp(MSG_TYPE_CREATE_OFFER, msg->type)) {
    pthis->create_offer_cb(pthis, pthis->callback_p);
  } else if (!strcmp(MSG_TYPE_SET_REMOTE_DESCRIPTION, msg->type)) {
    pthis->remote_answer_cb(pthis, msg->data, pthis->callback_p);
  } else {
    printf("webrtc_control: handle_msg: unknown message type %s\n", msg->type);
  }
  return 0;
}

static int recv_msg(struct webrtc_control_s* pthis, struct ctrl_msg_s* msg,
                     char* got_msg)
{
  int ret;
  while (1) {
    zmq_msg_t message;
    ret = zmq_msg_init (&message);
    ret = zmq_msg_recv (&message, pthis->pull_socket, 0);
    if (ret < 0 && EAGAIN == errno) {
      *got_msg = 0;
      zmq_msg_close(&message);
      return 0;
    }
    *got_msg = 1;
    // Process the message frame
    uint8_t* sz_msg = (uint8_t*)malloc(zmq_msg_size(&message) + 1);
    memcpy(sz_msg, zmq_msg_data(&message), zmq_msg_size(&message));
    sz_msg[zmq_msg_size(&message)] = '\0';

    if (!msg->type) {
      msg->type = (char*)sz_msg;
    } else if (!msg->data) {
      msg->data = (char*)sz_msg;
    } else {
      printf("unknown extra message part received. freeing.");
      free(sz_msg);
    }

    zmq_msg_close (&message);
    if (!zmq_msg_more (&message)) {
      break; // Last message frame
    }
  }
  return 0;
}

#define ONLINE_MSG "online"
static void zmq_main(void* p) {
  printf("webrtc_control online %p\n", p);
  struct webrtc_control_s* pthis = (struct webrtc_control_s*)p;
  // messaging runloop
  int t = 10;
  int ret = zmq_bind(pthis->pull_socket, "ipc:///tmp/webrtc_control-right");
  if (ret) {
    printf("webrtc_control: failed to connect to pull socket errno %d\n",
           errno);
    return;
  }
  ret = zmq_connect(pthis->push_socket, "ipc:///tmp/webrtc_control-left");
  if (ret) {
    printf("webrtc_control: failed to connect to push socket errno %d\n",
           errno);
    // this isn't fatal for the runloop, but will probably cause problems.
    // how should we handle it?
  }
  size_t len = strlen(ONLINE_MSG);
  ret = zmq_send(pthis->push_socket, ONLINE_MSG, len, 0);
  ret = zmq_setsockopt(pthis->pull_socket, ZMQ_RCVTIMEO, &t, sizeof(int));
  char got_msg = 0;
  struct ctrl_msg_s msg = { 0 };
  while (!pthis->is_interrupted) {
    ret = recv_msg(pthis, &msg, &got_msg);
    if (got_msg) {
      handle_msg(pthis, &msg);
      clear_msg(&msg);
    }
  }
  zmq_close(pthis->pull_socket);
}

void webrtc_control_alloc(struct webrtc_control_s** pthis_out) {
  struct webrtc_control_s* pthis = (struct webrtc_control_s*)
  calloc(1, sizeof(struct webrtc_control_s));
  pthis->cb_loop = uv_loop_new();
  pthis->zmq_ctx = zmq_ctx_new();
  pthis->push_socket = zmq_socket(pthis->zmq_ctx, ZMQ_PUSH);
  pthis->pull_socket = zmq_socket(pthis->zmq_ctx, ZMQ_PULL);

  *pthis_out = pthis;
}

void webrtc_control_free(struct webrtc_control_s* pthis) {
  free(pthis);
}

void webrtc_control_config(struct webrtc_control_s* pthis,
                           struct webrtc_control_config_s* config)
{
  pthis->callback_p = config->p;
  pthis->create_offer_cb = config->on_create_offer;
  pthis->remote_answer_cb = config->on_remote_answer;
}

int webrtc_control_start(struct webrtc_control_s* pthis) {
  pthis->is_interrupted = 0;
  pthis->is_running = 1;
  int ret = uv_thread_create(&pthis->zmq_thread, zmq_main, pthis);
  //int cbret = uv_thread_create(&pthis->cb_thread, cb_loop_main, pthis);
  return ret;
}

int webrtc_control_stop(struct webrtc_control_s* pthis) {
  int ret;
  if (!pthis->is_running) {
    return -1;
  }
  pthis->is_interrupted = 1;
  pthis->is_running = 0;
  do {
    ret = uv_loop_close(pthis->cb_loop);
  } while (UV_EBUSY == ret);
  pthis->cb_loop = NULL;
  ret = uv_thread_join(&pthis->zmq_thread);
  int get = uv_thread_join(&pthis->cb_thread);
  return ret | get;
}

#define MSG_TYPE_OFFER "offer"
int webrtc_control_send_offer(struct webrtc_control_s* pthis, const char* offer)
{
  int ret = zmq_send(pthis->push_socket, MSG_TYPE_OFFER,
                     strlen(MSG_TYPE_OFFER), ZMQ_SNDMORE);
  size_t len = strlen(offer);
  ret = zmq_send(pthis->push_socket, offer, len, 0);
  return len != ret;
}

#define MSG_TYPE_CANDIDATE "icecandidate"
int webrtc_control_send_ice_candidate(struct webrtc_control_s* pthis,
                                      const char* candidate)
{
  int ret = zmq_send(pthis->push_socket, MSG_TYPE_CANDIDATE,
                     strlen(MSG_TYPE_CANDIDATE), ZMQ_SNDMORE);
  size_t len = strlen(candidate);
  ret = zmq_send(pthis->push_socket, candidate, len, 0);
  return len != ret;
}
