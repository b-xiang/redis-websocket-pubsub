/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include <stdbool.h>

#include <event2/buffer.h>
#include <event2/event.h>

#include "status.h"

// Forwards declaration from http.h.
struct http_request;


struct websocket {
  char tmp;
};

struct websocket *websocket_init(void);
enum status       websocket_destroy(struct websocket *ws);
enum status       websocket_accept_http_request(const struct http_request *req, struct evbuffer *out, bool *accepted);

#endif  // WEBSOCKET_H_
