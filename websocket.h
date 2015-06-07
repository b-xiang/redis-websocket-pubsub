/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include "status.h"

// Forwards declaration from http.h.
struct http_request;


enum status websocket_write_http_response(const struct http_request *req, int fd);

#endif  // WEBSOCKET_H_
