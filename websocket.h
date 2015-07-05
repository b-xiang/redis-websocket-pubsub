/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>

#include <event2/buffer.h>
#include <event2/event.h>

#include "status.h"

// Forwards declaration from http.h.
struct http_request;

enum websocket_state {
  WS_CLOSED,
  WS_NEEDS_HTTP_UPGRADE,
  WS_NEEDS_INITIAL,
  WS_NEEDS_LENGTH_16,
  WS_NEEDS_LENGTH_64,
  WS_NEEDS_MASKING_KEY,
  WS_NEEDS_PAYLOAD,
};

enum websocket_opcode {
  WS_OPCODE_CONTINUATION_FRAME = 0x00,
  WS_OPCODE_TEXT_FRAME = 0x01,
  WS_OPCODE_BINARY_FRAME = 0x02,
  WS_OPCODE_CONNECTION_CLOSE = 0x08,
  WS_OPCODE_PING = 0x09,
  WS_OPCODE_PONG = 0x0a,
};

struct websocket {
  // The file descriptor of the socket.
  int fd;
  // Whether or not the socket has been shutdown.
  bool is_shutdown;
  // The address of the client.
  struct sockaddr_in addr;
  // The libevent bufferevent for the socket.
  struct bufferevent *bev;
  // The libevent output buffer.
  struct evbuffer *out;
  // The libevent unmasked input buffer for the current frame.
  struct evbuffer *in_frame;
  // THe libevent unmasked input buffer for the current message.
  struct evbuffer *in_message;
  // The state of the websocket input processing.
  enum websocket_state in_state;

  bool in_is_final;
  bool in_is_masked;
  uint8_t in_opcode;
  uint8_t in_reserved;
  uint32_t in_masking_key;
  uint64_t in_length;
};

struct websocket *websocket_init(int fd, const struct sockaddr_in *addr);
enum status       websocket_destroy(struct websocket *ws);
enum status       websocket_accept_http_request(struct websocket *ws, const struct http_request *req);
enum status       websocket_consume(struct websocket *ws, const uint8_t *bytes, size_t nbytes);
enum status       websocket_flush_output(struct websocket *ws);

#endif  // WEBSOCKET_H_
