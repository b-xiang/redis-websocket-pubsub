#ifndef CLIENT_CONNECTION_H_
#define CLIENT_CONNECTION_H_

#include <netinet/in.h>
#include <stdbool.h>

#include <event.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "status.h"


// Forwards declarations.
struct http_request;
struct http_response;
struct websocket;


struct client_connection {
  bool needs_shutdown;      // Whether or not the socket connection needs to be shutdown after the next write.
  bool is_shutdown;         // Whether or not the socket has been shutdown.
  int fd;                   // The file descriptor for the socket.
  struct sockaddr_in addr;  // The address of the client.

  // HTTP and WebSocket state.
  struct http_request *request;
  struct http_response *response;
  struct websocket *ws;

  // libevent events and state.
  struct event_base *event_loop;
  struct bufferevent *bev;  // The libevent bufferevent for the socket.

  // Linked list on client_connection events.
  struct client_connection *next;
};


struct client_connection *client_connection_create(struct event_base *event_loop, int fd, const struct sockaddr_in *addr);
void                      client_connection_destroy(struct client_connection *client);
void                      client_connection_destroy_all(void);
enum status               client_connection_shutdown(struct client_connection *client);

#endif  // CLIENT_CONNECTION_H_
