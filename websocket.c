/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#include "websocket.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "base64.h"
#include "client_connection.h"
#include "compat_endian.h"
#include "http.h"
#include "logging.h"

// From https://tools.ietf.org/html/rfc6455#section-4.2.2
static const char *const SEC_WEBSOCKET_KEY_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const uint64_t MAX_PAYLOAD_LENGTH = 16 * 1024 * 1024;  // 16MB.
static const struct timeval PING_INTERVAL = {.tv_sec = 30, .tv_usec = 0};

enum websocket_opcode {
  WS_OPCODE_CONTINUATION_FRAME = 0x00,
  WS_OPCODE_TEXT_FRAME = 0x01,
  WS_OPCODE_BINARY_FRAME = 0x02,
  WS_OPCODE_CONNECTION_CLOSE = 0x08,
  WS_OPCODE_PING = 0x09,
  WS_OPCODE_PONG = 0x0a,
};



// ================================================================================================
// Sending data across the WebSocket.
// ================================================================================================
static enum status
send_frame(struct websocket *const ws, const enum websocket_opcode opcode, struct evbuffer *const payload) {
  // Write the first two header bytes of the frame.
  uint8_t prefix[2];
  const uint64_t nbytes = evbuffer_get_length(payload);
  if (nbytes > UINT16_MAX) {
    prefix[1] = 127;
  }
  else if (nbytes > 125) {
    prefix[1] = 126;
  }
  else {
    prefix[1] = nbytes;
  }
  prefix[0] = 0x80 | ((uint8_t)opcode);
  evbuffer_add(ws->out, &prefix[0], 2);

  // Write an extended payload length if it's needed.
  if (nbytes > UINT16_MAX) {
    uint64_t length = htobe64(nbytes);
    evbuffer_add(ws->out, &length, 8);
  }
  else if (nbytes > 125) {
    uint16_t length = htobe16(nbytes);
    evbuffer_add(ws->out, &length, 2);
  }

  // Write the unmasked application data.
  evbuffer_add_buffer(ws->out, payload);

  // Flush the output buffer.
  return websocket_flush_output(ws);
}


static enum status
send_ping(struct websocket *const ws, struct evbuffer *const payload) {
  return send_frame(ws, WS_OPCODE_PING, payload);
}


static enum status
send_pong(struct websocket *const ws, struct evbuffer *const payload) {
  // "A Pong frame sent in response to a Ping frame must have identical "Application data" as
  // found in the message body of the Ping frame being replied to."
  return send_frame(ws, WS_OPCODE_PONG, payload);
}



// ================================================================================================
// libevent callbacks
// ================================================================================================
static void
on_timeout_sendping(const evutil_socket_t fd, const short events, void *const arg) {
  struct websocket *const ws = (struct websocket *)arg;
  DEBUG("on_timeout_sendping", "ws=%p fd=%d events=%d\n", ws, fd, events);

  // Don't send a PING control frame while if the WebSocket isn't established.
  if (ws->in_state == WS_NEEDS_HTTP_UPGRADE || ws->in_state == WS_CLOSED) {
    return;
  }

  // Drain the ping frame buffer and update its content for the next PING controlframe.
  evbuffer_drain(ws->ping_frame, evbuffer_get_length(ws->ping_frame));
  evbuffer_add_printf(ws->ping_frame, "%u", ws->ping_count++);

  send_ping(ws, ws->ping_frame);
}



static enum status
websocket_accept_http_request_reject(struct http_response *const response, const unsigned int status_code) {
  enum status status;

  status = http_response_set_status_code(response, status_code);
  if (status != STATUS_OK) {
    return status;
  }
  status = http_response_add_header(response, "Connection", "Close");
  if (status != STATUS_OK) {
    return status;
  }

  return status;
}


enum status
websocket_accept_http_request(struct websocket *const ws, struct http_response *const response, const struct http_request *const req) {
  struct base64_buffer sha1_base64_buffer;
  enum status status;
  const struct http_header *header = NULL;
  unsigned char *sha1_input_buffer = NULL;
  unsigned char sha1_output_buffer[SHA_DIGEST_LENGTH];

  if (ws == NULL || response == NULL || req == NULL) {
    return STATUS_EINVAL;
  }

  // Ensure we're talking HTTP/1.1 or higher.
  if (req->version_major != 1 || req->version_minor < 1) {
    return websocket_accept_http_request_reject(response, 505);
  }
  status = http_response_set_version(response, req->version_major, req->version_minor);
  if (status != STATUS_OK) {
    return status;
  }

  // Ensure we have an `Upgrade` header with the case-insensitive value `websocket`.
  header = http_request_find_header(req, "Upgrade");
  if (header == NULL || strcasecmp("websocket", header->value) != 0) {
    return websocket_accept_http_request_reject(response, 400);
  }

  // Ensure we have a `Connection` header with the case-insensitive value `Upgrade`.
  header = http_request_find_header(req, "Connection");
  if (header == NULL || strcasecmp("upgrade", header->value) != 0) {
    return websocket_accept_http_request_reject(response, 400);
  }

  // Look for the `Origin` HTTP header in the request.
  header = http_request_find_header(req, "Origin");
  if (header == NULL) {
    return websocket_accept_http_request_reject(response, 403);
  }

  // Ensure we have a `Sec-WebSocket-Version` header with a value of `13`.
  header = http_request_find_header(req, "Sec-WebSocket-Version");
  if (header == NULL || strcmp("13", header->value) != 0) {
    status = websocket_accept_http_request_reject(response, 400);
    if (status == STATUS_OK) {
      status = http_response_add_header(response, "Sec-WebSocket-Version", "13");
    }
    return status;
  }

  // Look for the `Sec-WebSocket-Key` HTTP header in the request.
  header = http_request_find_header(req, "Sec-WebSocket-Key");
  if (header == NULL) {
    return websocket_accept_http_request_reject(response, 400);
  }

  // Compute the SHA1 hash of the concatenation of the `Sec-WebSocket-Key` header and the hard-coded GUID.
  const size_t key_nbytes = strlen(header->value);
  const size_t guid_nbytes = strlen(SEC_WEBSOCKET_KEY_GUID);
  sha1_input_buffer = malloc(key_nbytes + guid_nbytes);
  if (sha1_input_buffer == NULL) {
    return STATUS_ENOMEM;
  }
  memcpy(sha1_input_buffer, header->value, key_nbytes);
  memcpy(sha1_input_buffer + key_nbytes, SEC_WEBSOCKET_KEY_GUID, guid_nbytes);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  SHA1(sha1_input_buffer, key_nbytes + guid_nbytes, sha1_output_buffer);
#pragma clang diagnostic pop
  free(sha1_input_buffer);

  // Convert the SHA1 hash bytes into its base64 representation.
  status = base64_init(&sha1_base64_buffer);
  if (status != STATUS_OK) {
    return status;
  }

  status = base64_encode((const char *)sha1_output_buffer, SHA_DIGEST_LENGTH, &sha1_base64_buffer);
  if (status != STATUS_OK) {
    return status;
  }

  // Send the server's opening handshake to accept the incomming connection.
  // https://tools.ietf.org/html/rfc6455#section-4.2.2
  http_response_set_status_code(response, 101);
  http_response_add_header(response, "Connection", "Upgrade");
  http_response_add_header(response, "Upgrade", "websocket");
  http_response_add_header_n(response, "Sec-WebSocket-Accept", 20, sha1_base64_buffer.data, sha1_base64_buffer.used);
  base64_destroy(&sha1_base64_buffer);

  if (status != STATUS_OK) {
    return status;
  }

  // The connection can now be upgraded to a websocket connection.
  ws->in_state = WS_NEEDS_INITIAL;
  bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);

  // Setup a periodic PING event.
  ws->ping_event = event_new(ws->client->event_loop, ws->client->fd, EV_PERSIST, &on_timeout_sendping, ws);
  if (ws->ping_event != NULL && event_add(ws->ping_event, &PING_INTERVAL) == -1) {
    WARNING0("websocket_accept_http_request", "`event_add` for ping_event failed.\n");
    event_del(ws->ping_event);
    event_free(ws->ping_event);
    ws->ping_event = NULL;
  }

  return STATUS_OK;
}


static void
consume_needs_initial(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 2);
  ws->in_frame_is_final = (bytes[0] >> 7) & 0x01;
  const uint8_t in_reserved = (bytes[0] >> 4) & 0x07;
  ws->in_frame_opcode = bytes[0] & 0x0f;
  const bool in_is_masked = (bytes[1] >> 7) & 0x01;
  ws->in_frame_nbytes = bytes[1] & 0x7f;

  // Validate the reserved bits and the masking flag.
  // "MUST be 0 unless an extension is negotiated that defines meanings for non-zero values."
  DEBUG("consume_needs_initial", "Received new frame header fin=%u reserved=%u opcode=%u is_masked=%u, length=%" PRIu64 "\n", ws->in_frame_is_final, in_reserved, ws->in_frame_opcode, in_is_masked, ws->in_frame_nbytes);
  if (in_reserved != 0) {
    ws->in_state = WS_CLOSED;
    return;
  }
  // "All frames sent to the server have this bit set to 1."
  if (!in_is_masked) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Change state depending on how many more bytes of length data we need to read in.
  if (ws->in_frame_nbytes == 126) {
    ws->in_state = WS_NEEDS_LENGTH_16;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
  }
  else if (ws->in_frame_nbytes == 127) {
    ws->in_state = WS_NEEDS_LENGTH_64;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 8, 8);
  }
  else {
    ws->in_state = WS_NEEDS_MASKING_KEY;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 4, 4);
  }

  // Close the connection if required.
  if (ws->in_frame_opcode == WS_OPCODE_CONNECTION_CLOSE) {
    DEBUG("consume_needs_payload", "Closing client on fd=%d due to CLOSE opcode.\n", ws->client->fd);
    ws->in_state = WS_CLOSED;
    return;
  }
}


static void
consume_needs_length_16(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  // Update our length and fail the connection if the payload size is too large.
  assert(nbytes == 2);
  ws->in_frame_nbytes = ntohs(*((uint16_t *)bytes));
  if (ws->in_frame_nbytes > MAX_PAYLOAD_LENGTH) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Update our state.
  ws->in_state = WS_NEEDS_MASKING_KEY;
  bufferevent_setwatermark(ws->client->bev, EV_READ, 4, 4);
}


static void
consume_needs_length_64(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 8);
  // Update our length and fail the connection if the payload size is too large.
  ws->in_frame_nbytes = be64toh(*((uint64_t *)bytes));
  if (ws->in_frame_nbytes > MAX_PAYLOAD_LENGTH) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Update our state.
  ws->in_state = WS_NEEDS_MASKING_KEY;
  bufferevent_setwatermark(ws->client->bev, EV_READ, 4, 4);
}


static void
consume_needs_masking_key(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 4);
  // Keep the masking key in network byte order as the de-masking algorithm requires network byte order.
  ws->in_frame_masking_key = *((uint32_t *)bytes);

  // Update our state.
  ws->in_state = WS_NEEDS_PAYLOAD;
  bufferevent_setwatermark(ws->client->bev, EV_READ, ws->in_frame_nbytes, ws->in_frame_nbytes);
}


static void
consume_needs_payload(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  const uint8_t *upto;
  uint8_t quad[4];
  size_t slice, nbytes_remaining;
  assert(nbytes == ws->in_frame_nbytes);

  // Drain the unmasked frame buffer and reserve space for the new data.
  evbuffer_drain(ws->in_frame_buffer, evbuffer_get_length(ws->in_frame_buffer));
  evbuffer_expand(ws->in_frame_buffer, nbytes);

  // Unmask the input data and copy it into the frame buffer.
  nbytes_remaining = nbytes;
  upto = bytes;
  while (nbytes_remaining != 0) {
    slice = (nbytes_remaining >= 4) ? 4 : nbytes_remaining;
    memcpy(quad, upto, slice);
    *((uint32_t *)&quad[0]) ^= ws->in_frame_masking_key;
    evbuffer_add(ws->in_frame_buffer, &quad[0], slice);
    upto += slice;
    nbytes_remaining -= slice;
  }

  // Update our state.
  switch (ws->in_frame_opcode) {
  case WS_OPCODE_CONTINUATION_FRAME:
    DEBUG("consume_needs_payload", "Received CONTINUATION frame on fd=%d. is_final=%d\n", ws->client->fd, ws->in_frame_is_final);
    // Ensure we are expecting a continuation frame.
    if (!ws->in_message_is_continuing) {
      ERROR0("consume_needs_payload", "Unexpected continuation frame. Closing WebSocket connection.\n");
      ws->in_state = WS_CLOSED;
      break;
    }

    // Copy the frame buffer into the message buffer.
    evbuffer_remove_buffer(ws->in_frame_buffer, ws->in_message_buffer, evbuffer_get_length(ws->in_frame_buffer));
    if (ws->in_frame_is_final) {
      ws->in_message_is_continuing = false;
      // Call the message callback.
      ws->in_message_cb(ws);
      // Drain the message buffer.
      evbuffer_drain(ws->in_message_buffer, evbuffer_get_length(ws->in_message_buffer));
    }

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_TEXT_FRAME:
    DEBUG("consume_needs_payload", "Received TEXT frame on fd=%d. is_final=%d\n", ws->client->fd, ws->in_frame_is_final);
    ws->in_message_is_binary = false;
    if (ws->in_frame_is_final) {
      ws->in_message_is_continuing = false;
      // Move all data from the frame buffer into the message buffer.
      evbuffer_add_buffer(ws->in_message_buffer, ws->in_frame_buffer);
      // Call the message callback.
      ws->in_message_cb(ws);
      // Drain the message buffer.
      evbuffer_drain(ws->in_message_buffer, evbuffer_get_length(ws->in_message_buffer));
    }
    else {
      ws->in_message_is_continuing = true;
      // Copy the frame buffer into the message buffer.
      evbuffer_remove_buffer(ws->in_frame_buffer, ws->in_message_buffer, evbuffer_get_length(ws->in_frame_buffer));
    }

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_BINARY_FRAME:
    DEBUG("consume_needs_payload", "Received BINARY frame on fd=%d. is_final=%d\n", ws->client->fd, ws->in_frame_is_final);
    ws->in_message_is_binary = true;
    if (ws->in_frame_is_final) {
      ws->in_message_is_continuing = false;
      // Move all data from the frame buffer into the message buffer.
      evbuffer_add_buffer(ws->in_frame_buffer, ws->in_message_buffer);
      // Call the message callback.
      ws->in_message_cb(ws);
      // Drain the message buffer.
      evbuffer_drain(ws->in_message_buffer, evbuffer_get_length(ws->in_message_buffer));
    }
    else {
      ws->in_message_is_continuing = true;
      // Copy the frame buffer into the message buffer.
      evbuffer_remove_buffer(ws->in_frame_buffer, ws->in_message_buffer, evbuffer_get_length(ws->in_frame_buffer));
    }

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_CONNECTION_CLOSE:
    DEBUG("consume_needs_payload", "Closing client on fd=%d due to CLOSE opcode.\n", ws->client->fd);
    // Close the connection.
    ws->in_state = WS_CLOSED;
    break;

  case WS_OPCODE_PING:
    DEBUG("consume_needs_payload", "Received PING from fd=%d. Sending PONG.\n", ws->client->fd);
    // "Upon receipt of a Ping frame, an endpoint MUST send a Pong frame in response, unless it
    // already received a Close frame. It SHOULD respond with Pong frame as soon as is practical."
    send_pong(ws, ws->in_frame_buffer);

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_PONG:
    DEBUG("consume_needs_payload", "Received PONG from fd=%d. Doing nothing.\n", ws->client->fd);
    // Don't do anything in response to receiving a pong frame.
    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->client->bev, EV_READ, 2, 2);
    break;

  default:
    // Close the connection since we received an unknown opcode.
    ERROR("consume_needs_payload", "Unknown opcode %u\n", ws->in_frame_opcode);
    ws->in_state = WS_CLOSED;
    break;
  }
}


enum status
websocket_consume(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  switch (ws->in_state) {
  case WS_NEEDS_INITIAL:
    consume_needs_initial(ws, bytes, nbytes);
    break;

  case WS_NEEDS_LENGTH_16:
    consume_needs_length_16(ws, bytes, nbytes);
    break;

  case WS_NEEDS_LENGTH_64:
    consume_needs_length_64(ws, bytes, nbytes);
    break;

  case WS_NEEDS_MASKING_KEY:
    consume_needs_masking_key(ws, bytes, nbytes);
    break;

  case WS_NEEDS_PAYLOAD:
    consume_needs_payload(ws, bytes, nbytes);
    break;

  default:
    ERROR("websocket_consume", "Unknown websocket state %d\n", ws->in_state);
    ws->in_state = WS_CLOSED;
    break;
  }

  return STATUS_OK;
}


struct websocket *
websocket_init(struct client_connection *const client, websocket_message_callback in_message_cb) {
  if (client == NULL || in_message_cb == NULL) {
    return NULL;
  }

  struct websocket *const ws = malloc(sizeof(struct websocket));
  if (ws == NULL) {
    return NULL;
  }

  memset(ws, 0, sizeof(struct websocket));
  ws->client = client;
  ws->out = evbuffer_new();
  ws->in_state = WS_NEEDS_HTTP_UPGRADE;
  ws->in_frame_buffer = evbuffer_new();
  ws->in_message_buffer = evbuffer_new();
  ws->in_message_cb = in_message_cb;
  ws->ping_frame = evbuffer_new();

  // Configure the buffers.
  if (ws->out == NULL || ws->in_frame_buffer == NULL || ws->in_message_buffer == NULL || ws->ping_frame == NULL) {
    websocket_destroy(ws);
    return NULL;
  }

  return ws;
}


enum status
websocket_destroy(struct websocket *const ws) {
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  if (ws->ping_event != NULL) {
    event_del(ws->ping_event);
    event_free(ws->ping_event);
  }
  if (ws->out != NULL) {
    evbuffer_free(ws->out);
  }
  if (ws->in_frame_buffer != NULL) {
    evbuffer_free(ws->in_frame_buffer);
  }
  if (ws->in_message_buffer != NULL) {
    evbuffer_free(ws->in_message_buffer);
  }
  if (ws->ping_frame != NULL) {
    evbuffer_free(ws->ping_frame);
  }
  free(ws);

  return STATUS_OK;
}


enum status
websocket_flush_output(struct websocket *const ws) {
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  if (bufferevent_write_buffer(ws->client->bev, ws->out) == -1) {
    return STATUS_BAD;
  }
  bufferevent_flush(ws->client->bev, EV_WRITE, BEV_FINISHED);

  return STATUS_OK;
}
