/**
 * The WebSocket protocol is defined in RFC4655
 * https://tools.ietf.org/html/rfc6455
 **/
#include "websocket.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "base64.h"
#include "http.h"

static const char *const SEC_WEBSOCKET_KEY_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";


static enum status
http_response_write_status_line(const unsigned int status_code, const char *const reason, const int fd) {
  if (reason == NULL) {
    return STATUS_EINVAL;
  }

  int ret = dprintf(fd, "HTTP/1.1 %u %s\r\n", status_code, reason);
  if (ret < 0) {
    return STATUS_BAD;
  }

  return STATUS_OK;
}


static enum status
http_response_write_no_body(const unsigned int status_code, const char *const reason, const int fd) {
  ssize_t nbytes;
  enum status status;

  status = http_response_write_status_line(status_code, reason, fd);
  if (status != STATUS_OK) {
    return status;
  }

  nbytes = write(fd, "\r\n", 2);
  if (nbytes == -1) {
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
websocket_write_http_response(const struct http_request *const req, const int fd) {
  (void)fd;
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
    http_response_write_no_body(505, "HTTP Version not supported", fd);
    return STATUS_OK;
  }

  // Ensure we have an `Upgrade` header with the case-insensitive value `websocket`.
  header = http_request_find_header(req, "UPGRADE");
  if (header == NULL || strcasecmp("websocket", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", fd);
    return STATUS_OK;
  }

  // Ensure we have a `Connection` header with the case-insensitive value `Upgrade`.
  header = http_request_find_header(req, "CONNECTION");
  if (header == NULL || strcasecmp("upgrade", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", fd);
    return STATUS_OK;
  }

  // Ensure we have a `Sec-WebSocket-Version` header with a value of `13`.
  header = http_request_find_header(req, "SEC-WEBSOCKET-VERSION");
  if (header == NULL || strcmp("13", header->value) != 0) {
    http_response_write_no_body(400, "Bad Request", fd);
    return STATUS_OK;
  }

  // Look for the `Sec-WebSocket-Key` HTTP header in the request.
  header = http_request_find_header(req, "SEC-WEBSOCKET-KEY");
  if (header == NULL) {
    http_response_write_no_body(400, "Bad Request", fd);
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
  SHA1(sha1_input_buffer, key_nbytes + guid_nbytes, sha1_output_buffer);
  free(sha1_input_buffer);

  fprintf(stdout, "sha1 output buffer:");
  for (size_t i = 0; i != SHA_DIGEST_LENGTH; ++i) {
    fprintf(stdout, " 0x%02x", sha1_output_buffer[i]);
  }
  fprintf(stdout, "\n");

  // Convert the SHA1 hash bytes into its base64 representation.
  base64_status = base64_init(&sha1_base64_buffer);
  if (base64_status != STATUS_OK) {
    return base64_status;
  }

  base64_status = base64_encode((const char *)sha1_output_buffer, SHA_DIGEST_LENGTH, &sha1_base64_buffer);
  if (base64_status != STATUS_OK) {
    return base64_status;
  }

  fprintf(stdout, "base64(sha1 output buffer): '");
  fwrite(sha1_base64_buffer.data, 1, sha1_base64_buffer.used, stdout);
  fprintf(stdout, "'\n");

  base64_status = base64_destroy(&sha1_base64_buffer);
  if (base64_status != STATUS_OK) {
    return base64_status;
  }

  return STATUS_OK;
}
