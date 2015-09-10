/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#pragma once

#include <stdint.h>

#include "status.h"
#include "uri.h"

// Forwards declaration from event2/buffer.h.
struct evbuffer;

// Forwards declaration from lexer.h.
struct lexer;


extern const char *const HTTP_METHOD_CONNECT;
extern const char *const HTTP_METHOD_DELETE;
extern const char *const HTTP_METHOD_GET;
extern const char *const HTTP_METHOD_HEAD;
extern const char *const HTTP_METHOD_OPTIONS;
extern const char *const HTTP_METHOD_POST;
extern const char *const HTTP_METHOD_PUT;
extern const char *const HTTP_METHOD_TRACE;


struct http_header {
  char *name;
  char *value;
  struct http_header *next;
};


struct http_request {
  uint32_t version_major;
  uint32_t version_minor;
  const char *method;
  struct uri uri;
  const char *uri_asterisk;
  const char *host;
  struct http_header *header;
};


struct http_response {
  uint32_t version_major;
  uint32_t version_minor;
  unsigned int status_code;
  struct http_header *header;
  const char *body;
};


struct http_request *http_request_init(void);
enum status          http_request_destroy(struct http_request *req);
enum status          http_request_add_header(struct http_request *req, const char *name, size_t name_nbytes, const char *value, size_t value_nbytes);
struct http_header * http_request_find_header(const struct http_request *req, const char *name);
enum status          http_request_parse(struct http_request *req, struct lexer *lex);

struct http_response *http_response_init(void);
enum status           http_response_destroy(struct http_response *response);
enum status           http_response_add_header(struct http_response *response, const char *name, const char *value);
enum status           http_response_add_header_n(struct http_response *response, const char *name, size_t name_nbytes, const char *value, size_t value_nbytes);
enum status           http_response_set_status_code(struct http_response *response, unsigned int status_code);
enum status           http_response_set_version(struct http_response *response, uint32_t version_major, uint32_t version_minor);
enum status           http_response_write_evbuffer(const struct http_response *response, struct evbuffer *out);
