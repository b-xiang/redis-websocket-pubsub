#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <event2/event.h>

#include "status.h"

// Forward declarations.
struct pubsub_manager;
struct websocket;


struct pubsub_manager *pubsub_manager_create(const char *redis_host, uint16_t redis_port, struct event_base *event_base);
enum status            pubsub_manager_destroy(struct pubsub_manager *mgr);
enum status            pubsub_manager_publish(struct pubsub_manager *mgr, const char *channel, const char *message);
enum status            pubsub_manager_publish_n(struct pubsub_manager *mgr, const char *channel, const char *message, size_t message_nbytes);
enum status            pubsub_manager_subscribe(struct pubsub_manager *mgr, const char *channel, struct websocket *ws);
enum status            pubsub_manager_unsubscribe(struct pubsub_manager *mgr, const char *channel, struct websocket *ws);
enum status            pubsub_manager_unsubscribe_all(struct pubsub_manager *mgr, struct websocket *ws);
