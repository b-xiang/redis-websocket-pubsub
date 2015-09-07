/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#include "websocket.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event.h>
#include <openssl/sha.h>

#include "base64.h"
#include "http.h"
#include "logging.h"

// From https://tools.ietf.org/html/rfc6455#section-4.2.2
static const char *const SEC_WEBSOCKET_KEY_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const uint64_t MAX_PAYLOAD_LENGTH = 16 * 1024 * 1024;  // 16MB.


static enum status
http_response_write_status_line(const unsigned int status_code, const char *const reason, struct evbuffer *const out) {
  if (reason == NULL) {
    return STATUS_EINVAL;
  }

  if (evbuffer_add_printf(out, "HTTP/1.1 %u %s\r\n", status_code, reason) == -1) {
    ERROR0("http_response_write_status_line", "`evbuffer_add_printf` failed\n");
    return STATUS_BAD;
  }

  return STATUS_OK;
}


static enum status
http_response_write_no_body(const unsigned int status_code, const char *const reason, struct evbuffer *const out) {
  enum status status;

  status = http_response_write_status_line(status_code, reason, out);
  if (status != STATUS_OK) {
    return status;
  }
  if (evbuffer_add_printf(out, "Connection: Close\r\n\r\n") == -1) {
    ERROR0("http_response_write_no_body", "`evbuffer_add_printf` failed\n");
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
websocket_accept_http_request(struct websocket *const ws, const struct http_request *const req) {
  struct base64_buffer sha1_base64_buffer;
  enum status base64_status;
  const struct http_request_header *header = NULL;
  unsigned char *sha1_input_buffer = NULL;
  unsigned char sha1_output_buffer[SHA_DIGEST_LENGTH];

  if (req == NULL) {
    return STATUS_EINVAL;
  }

  // Ensure we're talking HTTP/1.1 or higher.
  if (req->version_major != 1 || req->version_minor < 1) {
    http_response_write_no_body(405, "HTTP Version not supported", ws->out);
    return STATUS_OK;
  }

  // Ensure we have an `Upgrade` header with the case-insensitive value `websocket`.
  header = http_request_find_header(req, "UPGRADE");
  if (header == NULL || strcasecmp("websocket", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", ws->out);
    return STATUS_OK;
  }

  // Ensure we have a `Connection` header with the case-insensitive value `Upgrade`.
  header = http_request_find_header(req, "CONNECTION");
  if (header == NULL || strcasecmp("upgrade", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", ws->out);
    return STATUS_OK;
  }

  // Look for the `Origin` HTTP header in the request.
  header = http_request_find_header(req, "ORIGIN");
  if (header == NULL) {
    http_response_write_no_body(403, "Forbidden", ws->out);
    return STATUS_OK;
  }

  // Ensure we have a `Sec-WebSocket-Version` header with a value of `13`.
  header = http_request_find_header(req, "SEC-WEBSOCKET-VERSION");
  if (header == NULL || strcmp("13", header->value) != 0) {
    DEBUG("websocket_accept_http_request", "Sec-Websocket-Version out length = %zu\n", evbuffer_get_length(ws->out));
    http_response_write_no_body(400, "Bad Request", ws->out);
    DEBUG("websocket_accept_http_request", "Sec-Websocket-Version out length = %zu\n", evbuffer_get_length(ws->out));
    if (evbuffer_add_printf(ws->out, "Sec-WebSocket-Version: 13\r\n") == -1) {
      return STATUS_BAD;
    }
    DEBUG("websocket_accept_http_request", "Sec-Websocket-Version out length = %zu\n", evbuffer_get_length(ws->out));
    return STATUS_OK;
  }

  // Look for the `Sec-WebSocket-Key` HTTP header in the request.
  header = http_request_find_header(req, "SEC-WEBSOCKET-KEY");
  if (header == NULL) {
    http_response_write_no_body(400, "Bad Request", ws->out);
    return STATUS_OK;
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
  base64_status = base64_init(&sha1_base64_buffer);
  if (base64_status != STATUS_OK) {
    return base64_status;
  }

  base64_status = base64_encode((const char *)sha1_output_buffer, SHA_DIGEST_LENGTH, &sha1_base64_buffer);
  if (base64_status != STATUS_OK) {
    return base64_status;
  }

  // Send the server's opening handshake to accept the incomming connection.
  // https://tools.ietf.org/html/rfc6455#section-4.2.2
  if (evbuffer_add_printf(ws->out, "HTTP/1.1 101 Switching Protocols\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->out, "Upgrade: websocket\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->out, "Connection: Upgrade\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->out, "Sec-WebSocket-Accept: ") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add(ws->out, sha1_base64_buffer.data, sha1_base64_buffer.used) == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->out, "\r\n\r\n") == -1) {
    return STATUS_BAD;
  }

  base64_destroy(&sha1_base64_buffer);

  // The connection can now be upgraded to a websocket connection.
  ws->in_state = WS_NEEDS_INITIAL;
  bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);

  return STATUS_OK;
}


static enum status
websocket_send_frame(struct websocket *const ws, const enum websocket_opcode opcode, struct evbuffer *const payload) {
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
    uint64_t length = htonll(nbytes);
    evbuffer_add(ws->out, &length, 8);
  }
  else if (nbytes > 125) {
    uint16_t length = htons(nbytes);
    evbuffer_add(ws->out, &length, 2);
  }

  // Write the unmasked application data.
  evbuffer_add_buffer(ws->out, ws->in_frame);

  // Flush the output buffer.
  return websocket_flush_output(ws);
}


static enum status
websocket_send_pong(struct websocket *const ws) {
  // "A Pong frame sent in response to a Ping frame must have identical "Application data" as
  // found in the message body of the Ping frame being replied to."
  return websocket_send_frame(ws, WS_OPCODE_PONG, ws->in_frame);
}


static void
websocket_consume_needs_initial(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 2);
  ws->in_is_final = (bytes[0] >> 7) & 0x01;
  ws->in_reserved = (bytes[0] >> 4) & 0x07;
  ws->in_opcode = bytes[0] & 0x0f;
  ws->in_is_masked = (bytes[1] >> 7) & 0x01;
  ws->in_length = bytes[1] & 0x7f;

  // Validate the reserved bits and the masking flag.
  // "MUST be 0 unless an extension is negotiated that defines meanings for non-zero values."
  DEBUG("websocket_consume_needs_initial", "Received new frame header fin=%u reserved=%u opcode=%u is_masked=%u, length=%llu\n", ws->in_is_final, ws->in_reserved, ws->in_opcode, ws->in_is_masked, ws->in_length);
  if (ws->in_reserved != 0) {
    ws->in_state = WS_CLOSED;
    return;
  }
  // "All frames sent from to server have this bit set to 1."
  if (!ws->in_is_masked) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Change state depending on how many more bytes of length data we need to read in.
  if (ws->in_length == 126) {
    ws->in_state = WS_NEEDS_LENGTH_16;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
  }
  else if (ws->in_length == 127) {
    ws->in_state = WS_NEEDS_LENGTH_64;
    bufferevent_setwatermark(ws->bev, EV_READ, 8, 8);
  }
  else {
    ws->in_state = WS_NEEDS_MASKING_KEY;
    bufferevent_setwatermark(ws->bev, EV_READ, 4, 4);
  }
}


static void
websocket_consume_needs_length_16(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  // Update our length and fail the connection if the payload size is too large.
  assert(nbytes == 2);
  ws->in_length = ntohs(*((uint16_t *)bytes));
  if (ws->in_length > MAX_PAYLOAD_LENGTH) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Update our state.
  ws->in_state = WS_NEEDS_MASKING_KEY;
  bufferevent_setwatermark(ws->bev, EV_READ, 4, 4);
}


static void
websocket_consume_needs_length_64(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 8);
  // Update our length and fail the connection if the payload size is too large.
  ws->in_length = ntohll(*((uint64_t *)bytes));
  if (ws->in_length > MAX_PAYLOAD_LENGTH) {
    ws->in_state = WS_CLOSED;
    return;
  }

  // Update our state.
  ws->in_state = WS_NEEDS_MASKING_KEY;
  bufferevent_setwatermark(ws->bev, EV_READ, 4, 4);
}


static void
websocket_consume_needs_masking_key(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  assert(nbytes == 4);
  // Keep the masking key in network byte order as the de-masking algorithm requires network byte order.
  ws->in_masking_key = *((uint32_t *)bytes);

  // Update our state.
  ws->in_state = WS_NEEDS_PAYLOAD;
  bufferevent_setwatermark(ws->bev, EV_READ, ws->in_length, ws->in_length);
}


static void
websocket_consume_needs_payload(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  const uint8_t *upto;
  uint8_t quad[4];
  size_t slice, nbytes_remaining;
  assert(nbytes == ws->in_length);

  // Drain the unmasked frame buffer and reserve space for the new data.
  evbuffer_drain(ws->in_frame, evbuffer_get_length(ws->in_frame));
  evbuffer_expand(ws->in_frame, nbytes);

  // Unmask the input data and copy it into the frame buffer.
  nbytes_remaining = nbytes;
  upto = bytes;
  while (nbytes_remaining != 0) {
    slice = (nbytes_remaining >= 4) ? 4 : nbytes_remaining;
    memcpy(quad, upto, slice);
    *((uint32_t *)&quad[0]) ^= ws->in_masking_key;
    evbuffer_add(ws->in_frame, &quad[0], slice);
    upto += slice;
    nbytes_remaining -= slice;
  }

  // Update our state.
  switch (ws->in_opcode) {
  case WS_OPCODE_CONTINUATION_FRAME:
    DEBUG("websocket_consume_needs_payload", "Received CONTINUATION frame on fd=%d. is_final=%d\n", ws->fd, ws->in_is_final);
    // Ensure we have an existing message to continue on from.
    // TODO

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_TEXT_FRAME:
    DEBUG("websocket_consume_needs_payload", "Received TEXT frame on fd=%d. is_final=%d\n", ws->fd, ws->in_is_final);
    nbytes_remaining = evbuffer_get_length(ws->in_frame);
    DEBUG("websocket_consume_needs_payload", "evbuffer_get_length=>%zu\n", nbytes_remaining);
    uint8_t *text = malloc(nbytes_remaining + 1);
    assert(text != NULL);
    evbuffer_remove(ws->in_frame, text, nbytes_remaining);
    text[nbytes_remaining] = '\0';
    DEBUG("websocket_consume_needs_payload", "text='%s'\n", (const char *)text);
    free(text);
    // Ensure we don't have an existing message to continue on from.
    // TODO

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_BINARY_FRAME:
    DEBUG("websocket_consume_needs_payload", "Received BINARY frame on fd=%d. is_final=%d\n", ws->fd, ws->in_is_final);
    // Ensure we don't have an existing message to continue on from.
    // TODO

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_CONNECTION_CLOSE:
    DEBUG("websocket_consume_needs_payload", "Closing client on fd=%d due to CLOSE opcode.\n", ws->fd);
    // Close the connection.
    ws->in_state = WS_CLOSED;
    break;

  case WS_OPCODE_PING:
    DEBUG("websocket_consume_needs_payload", "Received PING from fd=%d. Sending PONG.\n", ws->fd);
    // "Upon receipt of a Ping frame, an endpoint MUST send a Pong frame in response, unless it
    // already received a Close frame. It SHOULD respond with Pong frame as soon as is practical."
    websocket_send_pong(ws);

    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
    break;

  case WS_OPCODE_PONG:
    DEBUG("websocket_consume_needs_payload", "Received PONG from fd=%d. Doing nothing.\n", ws->fd);
    // Don't do anything in response to receiving a pong frame.
    // Reset our state to waiting for a new frame.
    ws->in_state = WS_NEEDS_INITIAL;
    bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);
    break;

  default:
    // Close the connection since we received an unknown opcode.
    ERROR("websocket_consume_needs_payload", "Unknown opcode %u\n", ws->in_opcode);
    ws->in_state = WS_CLOSED;
    break;
  }
}


enum status
websocket_consume(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  switch (ws->in_state) {
  case WS_NEEDS_INITIAL:
    websocket_consume_needs_initial(ws, bytes, nbytes);
    break;

  case WS_NEEDS_LENGTH_16:
    websocket_consume_needs_length_16(ws, bytes, nbytes);
    break;

  case WS_NEEDS_LENGTH_64:
    websocket_consume_needs_length_64(ws, bytes, nbytes);
    break;

  case WS_NEEDS_MASKING_KEY:
    websocket_consume_needs_masking_key(ws, bytes, nbytes);
    break;

  case WS_NEEDS_PAYLOAD:
    websocket_consume_needs_payload(ws, bytes, nbytes);
    break;

  default:
    ERROR("websocket_consume", "Unknown websocket state %d\n", ws->in_state);
    ws->in_state = WS_CLOSED;
    break;
  }

  return STATUS_OK;
}


struct websocket *
websocket_init(int fd, const struct sockaddr_in *const addr) {
  struct websocket *const ws = malloc(sizeof(struct websocket));
  if (ws == NULL) {
    return NULL;
  }

  memset(ws, 0, sizeof(struct websocket));
  ws->fd = fd;
  ws->addr = *addr;
  ws->out = evbuffer_new();
  ws->in_frame = evbuffer_new();
  ws->in_message = evbuffer_new();
  ws->in_state = WS_NEEDS_HTTP_UPGRADE;

  // Configure the buffers.
  if (ws->out == NULL || ws->in_frame == NULL || ws->in_message == NULL) {
    websocket_destroy(ws);
    return NULL;
  }

  return ws;
}


enum status
websocket_destroy(struct websocket *const ws) {
  DEBUG("websocket_destroy", "ws=%p\n", ws);
  int ret;
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  if (ws->in_frame != NULL) {
    evbuffer_free(ws->in_frame);
  }
  if (ws->in_message != NULL) {
    evbuffer_free(ws->in_message);
  }
  if (ws->bev != NULL) {
    bufferevent_free(ws->bev);
  }
  if (ws->out != NULL) {
    evbuffer_free(ws->out);
  }
  if (ws->fd >= 0) {
    websocket_shutdown(ws);
    ret = close(ws->fd);
    WARNING("websocket_destroy", "`close` on fd=%d failed: %s\n", ws->fd, strerror(errno));
  }
  free(ws);

  return STATUS_OK;
}


enum status
websocket_flush_output(struct websocket *const ws) {
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  DEBUG("websocket_flush_output", "before length = %zu\n", evbuffer_get_length(ws->out));
  if (bufferevent_write_buffer(ws->bev, ws->out) == -1) {
    ERROR0("websocket_flush_output", "failed to flush output buffer (`bufferevent_write_buffer` failed)\n");
    return STATUS_BAD;
  }
  DEBUG("websocket_flush_output", "after length = %zu\n", evbuffer_get_length(ws->out));
  if (bufferevent_write(ws->bev, "Hello, World!\n", 14) == -1) {
    ERROR0("websocket_flush_output", "failed to flush output buffer (`bufferevent_write` failed)\n");
    return STATUS_BAD;
  }
  DEBUG("websocket_flush_output", "after length = %zu\n", evbuffer_get_length(ws->out));

  int ret = bufferevent_flush(ws->bev, EV_WRITE, BEV_FINISHED);
  DEBUG("websocket_flush_output", "buffeevent_flush = %d\n", ret);

  return STATUS_OK;
}


enum status
websocket_shutdown(struct websocket *const ws) {
  DEBUG("websocket_shutdown", "ws=%p\n", ws);
  int ret;
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  if (!ws->is_shutdown) {
    ret = shutdown(ws->fd, SHUT_RDWR);
    if (ret != 0) {
      WARNING("websocket_shutdown", "`shutdown` on fd=%d failed: %s\n", ws->fd, strerror(errno));
    }
  }

  return STATUS_OK;
}
