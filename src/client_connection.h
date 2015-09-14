#pragma once

#include <stdbool.h>

#include <event.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include <openssl/ssl.h>

#include "status.h"
#include "websocket.h"


// Forwards declarations.
struct http_request;
struct http_response;
struct pubsub_manager;


struct client_connection {
  bool needs_shutdown;      // Whether or not the socket connection needs to be shutdown after the next write.
  bool is_shutdown;         // Whether or not the socket has been shutdown.
  int fd;                   // The file descriptor for the socket.

  // HTTP and WebSocket state.
  struct http_request *request;
  struct http_response *response;
  struct websocket *ws;

  // State from the server.
  struct event_base *event_loop;
  SSL *ssl;

  // The libevent bufferevent for the socket.
  struct bufferevent *bev;

  // Keep track of the pubsub state so subscriptions can be unsubscribed from when disconnecting.
  struct pubsub_manager *pubsub_mgr;

  // Linked list on client_connection events.
  struct client_connection *next;
};


struct client_connection *client_connection_create(struct event_base *event_loop, SSL_CTX *ssl_ctx, int fd, struct pubsub_manager *pubsub_mgr, websocket_message_callback in_message_cb);
void                      client_connection_destroy(struct client_connection *client);
void                      client_connection_destroy_all(void);
enum status               client_connection_shutdown(struct client_connection *client);
