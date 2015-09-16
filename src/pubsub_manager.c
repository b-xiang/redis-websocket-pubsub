#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/event.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

#include "json.h"
#include "logging.h"
#include "pubsub_manager.h"
#include "string_pool.h"
#include "websocket.h"
#include "xxhash.h"

#define HASHTABLE_NBUCKETS (2063)  // Arbitrary "large enough" prime.


struct value_chain {
  void *value;
  struct value_chain *next;
};


struct key_chain {
  void *key;
  struct value_chain *chain;
  struct key_chain *next;
};


struct pubsub_manager {
  // Manage redis connection state.
  bool pub_is_connected;
  bool sub_is_connected;
  redisAsyncContext *pub_ctx;
  redisAsyncContext *sub_ctx;

  // Keep track of the libevent loop that the redis async connections are bound to.
  struct event_base *event_base;
  struct evbuffer *out_json_buffer;

  // Keep a string pool for quick hashtable lookup.
  struct string_pool *string_pool;

  // Keep track of the websocket <==> channel mappings.
  struct key_chain *channel_buckets[HASHTABLE_NBUCKETS];    // { channel : [ websocket ] }
  struct key_chain *websocket_buckets[HASHTABLE_NBUCKETS];  // { websocket : [ channel ] }
};


static void
hashtable_destroy(struct key_chain **const table, struct string_pool *const string_pool, const bool key_in_string_pool) {
  struct key_chain *key_chain, *next_key_chain;
  struct value_chain *value_chain, *next_value_chain;

  for (size_t i = 0; i != HASHTABLE_NBUCKETS; ++i) {
    for (key_chain = table[i]; key_chain != NULL; ) {
      next_key_chain = key_chain->next;
      for (value_chain = key_chain->chain; value_chain != NULL; ) {
        next_value_chain = value_chain->next;
        if (!key_in_string_pool) {
          string_pool_release(string_pool, (const char *)value_chain->value);
        }
        free(value_chain);
        value_chain = next_value_chain;
      }
      if (key_in_string_pool) {
        string_pool_release(string_pool, (const char *)key_chain->key);
      }
      free(key_chain);
      key_chain = next_key_chain;
    }
  }
}


static void
on_connect(const redisAsyncContext *const ctx, const int status) {
  struct pubsub_manager *const mgr = (struct pubsub_manager *)ctx->data;

  if (status != REDIS_OK) {
    ERROR("on_connect", "Error in on_connect redis callback. status=%d error=%s\n", status, ctx->errstr);
    return;
  }

  INFO("on_connect", "Connected to redis server. mgr=%p\n", (void *)mgr);
  if (ctx == mgr->pub_ctx) {
    mgr->pub_is_connected = true;
  }
  else {
    mgr->sub_is_connected = true;
  }
}


static void
on_disconnect(const redisAsyncContext *const ctx, const int status) {
  struct pubsub_manager *const mgr = (struct pubsub_manager *)ctx->data;
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

  INFO("on_connect", "Disconnected from redis server. mgr=%p\n", (void *)mgr);
}


static void
on_subscribed_reply_subscribe(struct pubsub_manager *const mgr, struct websocket *const ws, const char *const channel) {
  struct key_chain *key_chain, *prev_key_chain;
  struct value_chain *value_chain;
  size_t bucket;

  // Get a ref-counted canonical version of the channel string.
  const char *canonical_channel = string_pool_get(mgr->string_pool, channel);

  // Insert the websocket into the chain for the subscribed channel.
  bucket = XXH64(canonical_channel, strlen(canonical_channel), 0) % HASHTABLE_NBUCKETS;
  prev_key_chain = NULL;
  for (key_chain = mgr->channel_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == canonical_channel) {
      break;
    }
    prev_key_chain = key_chain;
  }

  // Did we find a websocket chain for this channel?
  if (key_chain == NULL) {
    // Create the key chain node for the channel to websockets mapping.
    key_chain = malloc(sizeof(struct key_chain));
    if (key_chain == NULL) {
      ERROR0("on_subscribed_reply_subscribe", "malloc failed.\n");
      string_pool_release(mgr->string_pool, canonical_channel);
      free(key_chain);
      return;
    }
    memset(key_chain, 0, sizeof(struct key_chain));
    key_chain->key = (void *)canonical_channel;
    canonical_channel = string_pool_get(mgr->string_pool, canonical_channel);

    // Insert it into the chain.
    if (prev_key_chain == NULL) {
      mgr->channel_buckets[bucket] = key_chain;
    }
    else {
      prev_key_chain->next = key_chain;
    }
  }

  // Insert the websocket into the chain.
  value_chain = malloc(sizeof(struct value_chain));
  if (value_chain == NULL) {
    ERROR0("on_subscribed_reply_subscribe", "malloc failed.\n");
    string_pool_release(mgr->string_pool, canonical_channel);
    return;
  }
  value_chain->value = ws;
  value_chain->next = key_chain->chain;
  key_chain->chain = value_chain;

  // Insert the channel into the chain for the websocket.
  bucket = ((size_t)ws) % HASHTABLE_NBUCKETS;
  prev_key_chain = NULL;
  for (key_chain = mgr->websocket_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == ws) {
      break;
    }
    prev_key_chain = key_chain;
  }

  // Did we find a channel chain for this websocket?
  if (key_chain == NULL) {
    // Create the key chain node for the channel to websockets mapping.
    key_chain = malloc(sizeof(struct key_chain));
    if (key_chain == NULL) {
      ERROR0("on_subscribed_reply_subscribe", "malloc failed.\n");
      free(key_chain);
      return;
    }
    memset(key_chain, 0, sizeof(struct key_chain));
    key_chain->key = (void *)ws;

    // Insert it into the chain.
    if (prev_key_chain == NULL) {
      mgr->websocket_buckets[bucket] = key_chain;
    }
    else {
      prev_key_chain->next = key_chain;
    }
  }

  // Insert the channel into the chain.
  value_chain = malloc(sizeof(struct value_chain));
  if (value_chain == NULL) {
    ERROR0("on_subscribed_reply_subscribe", "malloc failed.\n");
    return;
  }
  value_chain->value = (void *)canonical_channel;
  value_chain->next = key_chain->chain;
  key_chain->chain = value_chain;
}


static void
on_subscribed_reply_message(struct pubsub_manager *const mgr, const char *const channel, const char *const message) {
  struct key_chain *key_chain;
  struct value_chain *value_chain;
  struct websocket *ws;

  // Get a ref-counted canonical version of the channel string.
  const char *canonical_channel = string_pool_get(mgr->string_pool, channel);

  // Find the chain of websockets subscribed to the channel.
  const size_t bucket = XXH64(canonical_channel, strlen(canonical_channel), 0) % HASHTABLE_NBUCKETS;
  for (key_chain = mgr->channel_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == canonical_channel) {
      break;
    }
  }
  string_pool_release(mgr->string_pool, canonical_channel);
  if (key_chain == NULL) {
    return;
  }

  // Wrap the message in its JSON container.
  evbuffer_drain(mgr->out_json_buffer, evbuffer_get_length(mgr->out_json_buffer));
  evbuffer_add_printf(mgr->out_json_buffer, "{\"key\":");
  json_write_escape_string(mgr->out_json_buffer, channel);
  evbuffer_add_printf(mgr->out_json_buffer, ",\"data\":");
  json_write_escape_string(mgr->out_json_buffer, message);
  evbuffer_add_printf(mgr->out_json_buffer, "}");

  // Make the output JSON buffer a contiguous stream of bytes.
  const char *const out_json = (const char *)evbuffer_pullup(mgr->out_json_buffer, -1);
  const size_t out_json_nbytes = evbuffer_get_length(mgr->out_json_buffer);

  // Write the JSON message to each of the websockets.
  for (value_chain = key_chain->chain; value_chain != NULL; value_chain = value_chain->next) {
    ws = (struct websocket *)value_chain->value;
    DEBUG("on_subscribed_reply_message", "Sending to ws=%p via channel '%s'\n", (void *)ws, channel);
    websocket_send_text_bytes(ws, out_json, out_json_nbytes);
  }
}


static void
on_subscribed_reply(redisAsyncContext *const ctx, void *const _reply, void *const privdata) {
  struct pubsub_manager *const mgr = (struct pubsub_manager *)ctx->data;
  const redisReply *const reply = _reply;
  struct websocket *const ws = privdata;

  INFO("on_subscribed_reply", "mgr=%p reply=%p ws=%p\n", (void *)mgr, (void *)reply, (void *)ws);
  if (reply == NULL) {
    return;
  }
  else if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 3) {
    return;
  }
  DEBUG("on_subscribed_reply", "Received %s %s %s\n", reply->element[0]->str, reply->element[1]->str, reply->element[2]->str);

  if (strcmp(reply->element[0]->str, "subscribe") == 0) {
    on_subscribed_reply_subscribe(mgr, ws, reply->element[1]->str);
  }
  else if (strcmp(reply->element[0]->str, "message") == 0) {
    on_subscribed_reply_message(mgr, reply->element[1]->str, reply->element[2]->str);
  }
  else if (strcmp(reply->element[0]->str, "unsubscribe") == 0) {
    // Do nothing.
  }
  else {
    ERROR("on_subscribed_reply", "Received unknown message on subscription channel '%s' '%s' '%s'\n", reply->element[0]->str, reply->element[1]->str, reply->element[2]->str);
  }
}


struct pubsub_manager *
pubsub_manager_create(const char *const redis_host, const uint16_t redis_port, struct event_base *const event_base) {
  INFO("pubsub_manager_create", "Using hiredis version %d.%d.%d\n", HIREDIS_MAJOR, HIREDIS_MINOR, HIREDIS_PATCH);

  int status;
  if (redis_host == NULL) {
    return NULL;
  }

  // Setup the pubsub manager.
  struct pubsub_manager *const mgr = malloc(sizeof(struct pubsub_manager));
  if (mgr == NULL) {
    ERROR0("pubsub_manager_create", "malloc failed.\n");
    return NULL;
  }
  memset(mgr, 0, sizeof(struct pubsub_manager));
  mgr->event_base = event_base;
  mgr->out_json_buffer = evbuffer_new();
  mgr->string_pool = string_pool_create();
  if (mgr->out_json_buffer == NULL || mgr->string_pool == NULL) {
    string_pool_destroy(mgr->string_pool);
    evbuffer_free(mgr->out_json_buffer);
    free(mgr);
    return NULL;
  }

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
pubsub_manager_destroy(struct pubsub_manager *const mgr) {
  if (mgr == NULL) {
    return STATUS_EINVAL;
  }

  if (mgr->pub_is_connected) {
    redisAsyncDisconnect(mgr->pub_ctx);
  }
  if (mgr->sub_is_connected) {
    redisAsyncDisconnect(mgr->sub_ctx);
  }
  string_pool_destroy(mgr->string_pool);
  evbuffer_free(mgr->out_json_buffer);
  hashtable_destroy(mgr->channel_buckets, mgr->string_pool, true);
  hashtable_destroy(mgr->websocket_buckets, mgr->string_pool, false);
  free(mgr);

  return STATUS_OK;
}


enum status
pubsub_manager_publish(struct pubsub_manager *const mgr, const char *const channel, const char *const message) {
  return pubsub_manager_publish_n(mgr, channel, message, strlen(message));
}


enum status
pubsub_manager_publish_n(struct pubsub_manager *const mgr, const char *const channel, const char *const message, const size_t message_nbytes) {
  int status;

  if (mgr == NULL || channel == NULL || message == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->pub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  status = redisAsyncCommand(mgr->pub_ctx, NULL, NULL, "PUBLISH %s %b", channel, message, message_nbytes);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_publish_n", "async `PUBLISH %s` command failed. status=%d\n", channel, status);
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
pubsub_manager_subscribe(struct pubsub_manager *const mgr, const char *const channel, struct websocket *const ws) {
  int status;

  if (mgr == NULL || channel == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->sub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  // Has this websocket already subscribed to the channel?
  const size_t bucket = ((size_t)ws) % HASHTABLE_NBUCKETS;
  for (const struct key_chain *key_chain = mgr->websocket_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == ws) {
      // Get the canonical string for the channel name.
      const char *const canonical_channel = string_pool_get(mgr->string_pool, channel);
      for (struct value_chain *value_chain = key_chain->chain; value_chain != NULL; value_chain = value_chain->next) {
        if (value_chain->value == canonical_channel) {
          DEBUG("pubsub_manager_subscribe", "Not re-subscribing to channel '%s'\n", channel);
          string_pool_release(mgr->string_pool, canonical_channel);
          return STATUS_OK;
        }
      }
      string_pool_release(mgr->string_pool, canonical_channel);
      break;
    }
  }

  DEBUG("pubsub_manager_subscribe", "Subscribing to channel '%s'\n", channel);
  status = redisAsyncCommand(mgr->sub_ctx, &on_subscribed_reply, ws, "SUBSCRIBE %s", channel);
  if (status != REDIS_OK) {
    ERROR("pubsub_manager_publish_n", "async `SUBSCRIBE %s` command failed. status=%d\n", channel, status);
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
remove_websocket_from_channel_chain(struct pubsub_manager *const mgr, const char *const canonical_channel, struct websocket *const ws) {
  int redis_status;
  struct key_chain *key_chain, *prev_key_chain, *next_key_chain;
  struct value_chain *value_chain, *prev_value_chain, *next_value_chain;
  enum status status = STATUS_OK;

  // Remove the websocket from the channel_buckets chain.
  const size_t bucket = XXH64(canonical_channel, strlen(canonical_channel), 0) % HASHTABLE_NBUCKETS;
  prev_key_chain = NULL;
  for (key_chain = mgr->channel_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == canonical_channel) {
      break;
    }
    prev_key_chain = key_chain;
  }
  assert(key_chain != NULL);  // If this happens, the two hash tables are in an inconsistent state.

  // Remove the websocket from the list of websockets listening to the channel.
  prev_value_chain = NULL;
  for (value_chain = key_chain->chain; value_chain != NULL; value_chain = value_chain->next) {
    if (value_chain->value == ws) {
      next_value_chain = value_chain->next;

      free(value_chain);

      if (prev_value_chain == NULL) {
        key_chain->chain = next_value_chain;
      }
      else {
        prev_value_chain->next = next_value_chain;
      }
      break;
    }
    prev_value_chain = value_chain;
  }

  // If there aren't any websockets left that listen to the channel, remove it.
  if (key_chain->chain == NULL) {
    // Unsubscribe from the channel.
    redis_status = redisAsyncCommand(mgr->sub_ctx, NULL, NULL, "UNSUBSCRIBE %s", canonical_channel);
    if (redis_status != REDIS_OK) {
      ERROR("remove_websocket_from_channel_chain", "async `UNSUBSCRIBE %s` command failed. status=%d\n", canonical_channel, redis_status);
      status = STATUS_BAD;
    }

    // Remove it.
    next_key_chain = key_chain->next;

    string_pool_release(mgr->string_pool, (const char *)key_chain->key);
    free(key_chain);

    if (prev_key_chain == NULL) {
      mgr->channel_buckets[bucket] = next_key_chain;
    }
    else {
      prev_key_chain->next = next_key_chain;
    }
  }

  return status;
}


enum status
pubsub_manager_unsubscribe(struct pubsub_manager *const mgr, const char *const channel, struct websocket *const ws) {
  struct key_chain *key_chain, *prev_key_chain, *next_key_chain;
  struct value_chain *value_chain, *prev_value_chain, *next_value_chain;
  size_t bucket;

  if (mgr == NULL || channel == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->sub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  // Get the canonical string for the channel name.
  const char *const canonical_channel = string_pool_get(mgr->string_pool, channel);

  // Remove the channel from the websocket_buckets chain.
  bucket = ((size_t)ws) % HASHTABLE_NBUCKETS;
  prev_key_chain = NULL;
  for (key_chain = mgr->websocket_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == ws) {
      break;
    }
    prev_key_chain = key_chain;
  }
  if (key_chain == NULL) {
    string_pool_release(mgr->string_pool, canonical_channel);
    return STATUS_OK;
  }

  // Remove the node from the websocket to channel chain list.
  prev_value_chain = NULL;
  for (value_chain = key_chain->chain; value_chain != NULL; value_chain = value_chain->next) {
    if (value_chain->value == canonical_channel) {
      next_value_chain = value_chain->next;

      string_pool_release(mgr->string_pool, (const char *)value_chain->value);
      free(value_chain);

      if (prev_value_chain == NULL) {
        key_chain->chain = next_value_chain;
      }
      else {
        prev_value_chain->next = next_value_chain;
      }
      break;
    }
    prev_value_chain = value_chain;
  }

  // If there aren't any channels left that the websocket has subscribed to, remove it.
  if (key_chain->chain == NULL) {
    next_key_chain = key_chain->next;

    free(key_chain);

    if (prev_key_chain == NULL) {
      mgr->websocket_buckets[bucket] = next_key_chain;
    }
    else {
      prev_key_chain->next = next_key_chain;
    }
  }

  // Remove the websocket from the channel_buckets chain.
  const enum status status = remove_websocket_from_channel_chain(mgr, canonical_channel, ws);

  string_pool_release(mgr->string_pool, canonical_channel);
  return status;
}


enum status
pubsub_manager_unsubscribe_all(struct pubsub_manager *const mgr, struct websocket *const ws) {
  struct key_chain *key_chain, *prev_key_chain;
  struct value_chain *value_chain, *next_value_chain;

  if (mgr == NULL) {
    return STATUS_EINVAL;
  }
  else if (!mgr->sub_is_connected) {
    return STATUS_DISCONNECTED;
  }

  // Remove the channel from the websocket_buckets chain.
  const size_t bucket = ((size_t)ws) % HASHTABLE_NBUCKETS;
  prev_key_chain = NULL;
  for (key_chain = mgr->websocket_buckets[bucket]; key_chain != NULL; key_chain = key_chain->next) {
    if (key_chain->key == ws) {
      break;
    }
    prev_key_chain = key_chain;
  }
  if (key_chain == NULL) {
    return STATUS_OK;
  }

  // Remove the websocket from the channel_buckets chain.
  enum status status = STATUS_OK, s;
  for (value_chain = key_chain->chain; value_chain != NULL; ) {
    next_value_chain = value_chain->next;

    s = remove_websocket_from_channel_chain(mgr, (const char *)value_chain->value, ws);
    if (s != STATUS_OK) {
      status = s;
    }

    string_pool_release(mgr->string_pool, (const char *)value_chain->value);
    free(value_chain);

    value_chain = next_value_chain;
  }

  // Update the key chain list and free the current key chain.
  if (prev_key_chain == NULL) {
    mgr->websocket_buckets[bucket] = key_chain->next;
  }
  else {
    prev_key_chain->next = key_chain->next;
  }
  free(key_chain);

  return status;
}
