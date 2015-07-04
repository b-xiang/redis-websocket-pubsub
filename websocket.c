/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#include "websocket.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event.h>
#include <openssl/sha.h>

#include "base64.h"
#include "http.h"

// From https://tools.ietf.org/html/rfc6455#section-4.2.2
static const char *const SEC_WEBSOCKET_KEY_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const uint64_t MAX_PAYLOAD_LENGTH = 16 * 1024 * 1024;  // 16MB.

const uint8_t OPCODE_CONTINUATION_FRAME = 0x00;
const uint8_t OPCODE_TEXT_FRAME = 0x01;
const uint8_t OPCODE_BINARY_FRAME = 0x02;
const uint8_t OPCODE_CONNECTION_CLOSE = 0x08;
const uint8_t OPCODE_PING = 0x09;
const uint8_t OPCODE_PONG = 0x0a;


static enum status
http_response_write_status_line(const unsigned int status_code, const char *const reason, struct evbuffer *const out) {
  if (reason == NULL) {
    return STATUS_EINVAL;
  }

  if (evbuffer_add_printf(out, "HTTP/1.1 %u %s\r\n", status_code, reason) == -1) {
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

  if (evbuffer_add_printf(out, "\r\n") == -1) {
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
    http_response_write_no_body(505, "HTTP Version not supported", ws->buf_out);
    return STATUS_OK;
  }

  // Ensure we have an `Upgrade` header with the case-insensitive value `websocket`.
  header = http_request_find_header(req, "UPGRADE");
  if (header == NULL || strcasecmp("websocket", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", ws->buf_out);
    return STATUS_OK;
  }

  // Ensure we have a `Connection` header with the case-insensitive value `Upgrade`.
  header = http_request_find_header(req, "CONNECTION");
  if (header == NULL || strcasecmp("upgrade", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", ws->buf_out);
    return STATUS_OK;
  }

  // Look for the `Origin` HTTP header in the request.
  header = http_request_find_header(req, "ORIGIN");
  if (header == NULL) {
    http_response_write_no_body(403, "Forbidden", ws->buf_out);
    return STATUS_OK;
  }

  // Ensure we have a `Sec-WebSocket-Version` header with a value of `13`.
  header = http_request_find_header(req, "SEC-WEBSOCKET-VERSION");
  if (header == NULL || strcmp("13", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", ws->buf_out);
    if (evbuffer_add_printf(ws->buf_out, "Sec-WebSocket-Version: 13\r\n") == -1) {
      return STATUS_BAD;
    }
    return STATUS_OK;
  }

  // Look for the `Sec-WebSocket-Key` HTTP header in the request.
  header = http_request_find_header(req, "SEC-WEBSOCKET-KEY");
  if (header == NULL) {
    http_response_write_no_body(400, "Bad Request", ws->buf_out);
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
  if (evbuffer_add_printf(ws->buf_out, "HTTP/1.1 101 Switching Protocols\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->buf_out, "Upgrade: websocket\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->buf_out, "Connection: Upgrade\r\n") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->buf_out, "Sec-WebSocket-Accept: ") == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add(ws->buf_out, sha1_base64_buffer.data, sha1_base64_buffer.used) == -1) {
    return STATUS_BAD;
  }
  if (evbuffer_add_printf(ws->buf_out, "\r\n\r\n") == -1) {
    return STATUS_BAD;
  }

  base64_destroy(&sha1_base64_buffer);

  // The connection can now be upgraded to a websocket connection.
  ws->in_state = WS_NEEDS_INITIAL;
  bufferevent_setwatermark(ws->bev, EV_READ, 2, 2);

  return STATUS_OK;
}


enum status
websocket_consume(struct websocket *const ws, const uint8_t *const bytes, const size_t nbytes) {
  (void)ws;
  (void)bytes;
  (void)nbytes;
  (void)close;
  switch (ws->in_state) {
  case WS_NEEDS_INITIAL:
    // In order to get to this state, the read watermark had been set to 2, so we should only ever
    // have 2 bytes of input.
    assert(nbytes == 2);
    ws->in_is_final = (bytes[0] >> 7) & 0x01;
    ws->in_reserved = (bytes[0] >> 4) & 0x07;
    ws->in_opcode = bytes[0] & 0x0f;
    ws->in_is_masked = (bytes[1] >> 7) & 0x01;
    ws->in_length = bytes[1] & 0x7f;

    // Validate the reserved bits and the masking flag.
    // "MUST be 0 unless an extension is negotiated that defines meanings for non-zero values."
    fprintf(stderr, "[DEBUG] fin=%u reserved=%u opcode=%u is_masked=%u, length=%llu\n", ws->in_is_final, ws->in_reserved, ws->in_opcode, ws->in_is_masked, ws->in_length);
    if (ws->in_reserved != 0) {
      ws->in_state = WS_CLOSED;
      break;
    }
    // "All frames sent from to server have this bit set to 1."
    if (!ws->in_is_masked) {
      ws->in_state = WS_CLOSED;
      break;
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
    break;

  case WS_NEEDS_LENGTH_16:
    // Update our length and fail the connection if the payload size is too large.
    assert(nbytes == 2);
    ws->in_length = ntohs(*((uint16_t *)bytes));
    if (ws->in_length > MAX_PAYLOAD_LENGTH) {
      ws->in_state = WS_CLOSED;
      break;
    }

    // Update our state.
    ws->in_state = WS_NEEDS_MASKING_KEY;
    bufferevent_setwatermark(ws->bev, EV_READ, 4, 4);
    break;

  case WS_NEEDS_LENGTH_64:
    assert(nbytes == 8);
    // Update our length and fail the connection if the payload size is too large.
    ws->in_length = ntohll(*((uint64_t *)bytes));
    if (ws->in_length > MAX_PAYLOAD_LENGTH) {
      ws->in_state = WS_CLOSED;
      break;
    }

    // Update our state.
    ws->in_state = WS_NEEDS_MASKING_KEY;
    bufferevent_setwatermark(ws->bev, EV_READ, 4, 4);
    break;

  case WS_NEEDS_MASKING_KEY:
    assert(nbytes == 4);
    ws->in_masking_key = ntohl(*((uint32_t *)bytes));
    fprintf(stderr, "[DEBUG] masking key=0x%x\n", ws->in_masking_key);

    // Update our state.
    ws->in_state = WS_NEEDS_PAYLOAD;
    bufferevent_setwatermark(ws->bev, EV_READ, ws->in_length, ws->in_length);
    break;

  default:
    fprintf(stderr, "[ERROR] Unknown websocket state %d\n", ws->in_state);
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
  ws->buf_out = evbuffer_new();
  ws->in_state = WS_NEEDS_HTTP_UPGRADE;

  // Configure the output buffer.
  if (ws->buf_out == NULL) {
    websocket_destroy(ws);
    return NULL;
  }

  return ws;
}


enum status
websocket_destroy(struct websocket *const ws) {
  int ret;
  if (ws == NULL) {
    return STATUS_EINVAL;
  }

  if (ws->buf_out != NULL) {
    evbuffer_free(ws->buf_out);
  }
  if (ws->bev != NULL) {
    bufferevent_free(ws->bev);
  }
  if (!ws->is_shutdown) {
    ret = shutdown(ws->fd, SHUT_RDWR);
    if (ret != 0) {
      fprintf(stderr, "[WARNING] `shutdown` on fd=%d failed: %s\n", ws->fd, strerror(errno));
    }
  }
  if (ws->fd >= 0) {
    ret = close(ws->fd);
    fprintf(stderr, "[WARNING] `close` on fd=%d failed: %s\n", ws->fd, strerror(errno));
  }
  free(ws);

  return STATUS_OK;
}
