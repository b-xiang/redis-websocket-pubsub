/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#pragma once

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
struct websocket;


typedef void (*websocket_message_callback)(struct websocket *ws);

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
  uint8_t in_frame_is_final;
  uint8_t in_frame_opcode;
  uint8_t in_message_is_binary;
  uint8_t in_message_is_continuing;
  uint32_t in_frame_masking_key;
  uint64_t in_frame_nbytes;
  struct evbuffer *in_frame_buffer;    // The libevent unmasked input buffer for the current frame.
  struct evbuffer *in_message_buffer;  // The libevent unmasked input buffer for the current message.
  websocket_message_callback in_message_cb;

  // PING state.
  uint32_t ping_count;
  struct evbuffer *ping_frame;
};


struct websocket *websocket_init(struct client_connection *client, websocket_message_callback in_message_cb);
enum status       websocket_destroy(struct websocket *ws);
enum status       websocket_accept_http_request(struct websocket *ws, struct http_response *response, const struct http_request *req);
enum status       websocket_consume(struct websocket *ws, const uint8_t *bytes, size_t nbytes);
enum status       websocket_flush_output(struct websocket *ws);
