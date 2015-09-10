#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

#include "logging.h"
#include "redis_pubsub.h"
#include "websocket.h"


struct redis_pubsub_manager {
  bool pub_is_connected;
  bool sub_is_connected;
  redisAsyncContext *pub_ctx;
  redisAsyncContext *sub_ctx;
  struct event_base *event_base;
};


static void
on_connect(const redisAsyncContext *const ctx, const int status) {
  struct redis_pubsub_manager *const mgr = (struct redis_pubsub_manager *)ctx->data;

  if (status != REDIS_OK) {
    ERROR("on_connect", "Error in on_connect redis callback. status=%d error=%s\n", status, ctx->errstr);
    return;
  }

  INFO("on_connect", "Connected to redis server. mgr=%p\n", mgr);
  if (ctx == mgr->pub_ctx) {
    mgr->pub_is_connected = true;
  }
  else {
    mgr->sub_is_connected = true;
  }
}


static void
on_disconnect(const redisAsyncContext *const ctx, const int status) {
  struct redis_pubsub_manager *const mgr = (struct redis_pubsub_manager *)ctx->data;
  if (ctx == mgr->pub_ctx) {
    mgr->pub_is_connected = false;
  }
  else {
    mgr->sub_is_connected = false;
  }

  if (status != REDIS_OK) {
    ERROR("on_connect", "Error in on_disconnect redis callback. status=%d error=%s\n", status, ctx->errstr);
    return;
  }

  INFO("on_connect", "Disconnected from redis server. mgr=%p\n", mgr);
}


static void
on_publish(redisAsyncContext *const ctx, void *const _reply, void *const privdata) {
  struct redis_pubsub_manager *const mgr = (struct redis_pubsub_manager *)ctx->data;
  const redisReply *const reply = _reply;

  INFO("on_publish", "mgr=%p reply=%p privdata=%p\n", mgr, reply, privdata);
}


static void
on_subscribe(redisAsyncContext *const ctx, void *const _reply, void *const privdata) {
  struct redis_pubsub_manager *const mgr = (struct redis_pubsub_manager *)ctx->data;
  const redisReply *const reply = _reply;
  struct websocket *const ws = privdata;

  INFO("on_subscribe", "mgr=%p reply=%p ws=%p\n", mgr, reply, ws);
  if (reply == NULL) {
    return;
  }
  else if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 3) {
    return;
  }

  DEBUG("on_subscribe", "Received %s %s %s\n", reply->element[0]->str, reply->element[1]->str, reply->element[2]->str);
}


struct redis_pubsub_manager *
pubsub_manager_create(const char *const redis_host, const uint16_t redis_port, struct event_base *const event_base) {
  int status;
  if (redis_host == NULL) {
    return NULL;
  }

  // Setup the pubsub manager.
  struct redis_pubsub_manager *const mgr = malloc(sizeof(struct redis_pubsub_manager));
  if (mgr == NULL) {
    ERROR0("pubsub_manager_create", "malloc failed.\n");
    return NULL;
  }
  memset(mgr, 0, sizeof(struct redis_pubsub_manager));
  mgr->event_base = event_base;

  // Connect to the redis server.
  mgr->pub_ctx = redisAsyncConnect(redis_host, redis_port);
  mgr->sub_ctx = redisAsyncConnect(redis_host, redis_port);
  if (mgr->pub_ctx == NULL || mgr->sub_ctx == NULL) {
    ERROR("pubsub_manager_create", "Failed to connect to redis server %s:%d\n", redis_host, redis_port);
    goto fail;
  }
  // Set the redis async context's user data attribute to be our manager object.
  mgr->pub_ctx->data = mgr;
  mgr->sub_ctx->data = mgr;

  // Attach the redis async connection to the libevent event loop.
  status = redisLibeventAttach(mgr->pub_ctx, mgr->event_base);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisLibeventAttach`. status=%d\n", status);
    goto fail;
  }
  status = redisLibeventAttach(mgr->sub_ctx, mgr->event_base);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisLibeventAttach`. status=%d\n", status);
    goto fail;
  }

  // Setup the redis connect/disconnect callbacks.
  status = redisAsyncSetConnectCallback(mgr->pub_ctx, &on_connect);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisAsyncSetConnectCallback`. status=%d\n", status);
    goto fail;
  }
  status = redisAsyncSetDisconnectCallback(mgr->pub_ctx, &on_disconnect);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisAsyncSetDisconnectCallback`. status=%d\n", status);
    goto fail;
  }
  status = redisAsyncSetConnectCallback(mgr->sub_ctx, &on_connect);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisAsyncSetConnectCallback`. status=%d\n", status);
    goto fail;
  }
  status = redisAsyncSetDisconnectCallback(mgr->sub_ctx, &on_disconnect);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_create", "Failed to `redisAsyncSetDisconnectCallback`. status=%d\n", status);
    goto fail;
  }

  return mgr;

fail:
  if (mgr->pub_ctx != NULL) {
    redisAsyncDisconnect(mgr->pub_ctx);
  }
  if (mgr->sub_ctx != NULL) {
    redisAsyncDisconnect(mgr->sub_ctx);
  }
  free(mgr);
  return NULL;
}


enum status
pubsub_manager_destroy(struct redis_pubsub_manager *const mgr) {
  if (mgr == NULL) {
    return STATUS_EINVAL;
  }

  if (mgr->pub_is_connected) {
    redisAsyncDisconnect(mgr->pub_ctx);
  }
  if (mgr->sub_is_connected) {
    redisAsyncDisconnect(mgr->sub_ctx);
  }
  free(mgr);

  return STATUS_OK;
}


enum status
pubsub_manager_publish(struct redis_pubsub_manager *const mgr, const char *const channel, const char *const message) {
  return pubsub_manager_publish_n(mgr, channel, message, strlen(message));
}


enum status
pubsub_manager_publish_n(struct redis_pubsub_manager *const mgr, const char *const channel, const char *const message, const size_t message_nbytes) {
  int status;

  if (mgr == NULL || channel == NULL || message == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->pub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  status = redisAsyncCommand(mgr->pub_ctx, &on_publish, NULL, "PUBLISH %s %b", channel, message, message_nbytes);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_publish_n", "async `PUBLISH %s` command failed. status=%d\n", channel, status);
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
pubsub_manager_subscribe(struct redis_pubsub_manager *const mgr, const char *const channel, struct websocket *const ws) {
  int status;

  if (mgr == NULL || channel == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->sub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  // TODO check whether or not we have already subscribed to the channel.
  status = redisAsyncCommand(mgr->sub_ctx, &on_subscribe, ws, "SUBSCRIBE %s", channel);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_publish_n", "async `SUBSCRIBE %s` command failed. status=%d\n", channel, status);
    return STATUS_BAD;
  }

  return STATUS_OK;
}
