/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>

#include <event.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "status.h"


// Forwards declarations.
struct client_connection;
struct http_request;
struct http_response;


enum websocket_state {
  WS_CLOSED,
  WS_NEEDS_HTTP_UPGRADE,
  WS_NEEDS_INITIAL,
  WS_NEEDS_LENGTH_16,
  WS_NEEDS_LENGTH_64,
  WS_NEEDS_MASKING_KEY,
  WS_NEEDS_PAYLOAD,
};


struct websocket {
  // Client connection state.
  struct client_connection *client;  // The connection to the client.

  // libevent state.
  struct event *ping_event;     // Timeout event to send ping control frame.s
  struct evbuffer *out;         // The libevent output buffer.

  // Input processing state.
  enum websocket_state in_state;  // The state of the websocket input processing.
  bool in_is_final;
  bool in_is_masked;
  uint8_t in_opcode;
  uint8_t in_reserved;
  uint32_t in_masking_key;
  uint64_t in_length;
  struct evbuffer *in_frame;    // The libevent unmasked input buffer for the current frame.
  struct evbuffer *in_message;  // The libevent unmasked input buffer for the current message.

  // PING state.
  unsigned int ping_count;
  struct evbuffer *ping_frame;
};


struct websocket *websocket_init(struct client_connection *client);
enum status       websocket_destroy(struct websocket *ws);
enum status       websocket_accept_http_request(struct websocket *ws, struct http_response *response, const struct http_request *req);
enum status       websocket_consume(struct websocket *ws, const uint8_t *bytes, size_t nbytes);
enum status       websocket_flush_output(struct websocket *ws);

#endif  // WEBSOCKET_H_
