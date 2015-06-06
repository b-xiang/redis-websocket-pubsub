/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#ifndef HTTP_H_
#define HTTP_H_

#include <stdint.h>

#include "uri.h"

// Forwards declaration from lexer.h.
struct lexer;


enum http_request_parse_status {
  HTTP_REQUEST_PARSE_BAD = 0,
  HTTP_REQUEST_PARSE_OK,
  HTTP_REQUEST_PARSE_ENOMEM,
};


extern const char *const HTTP_METHOD_CONNECT;
extern const char *const HTTP_METHOD_DELETE;
extern const char *const HTTP_METHOD_GET;
extern const char *const HTTP_METHOD_HEAD;
extern const char *const HTTP_METHOD_OPTIONS;
extern const char *const HTTP_METHOD_POST;
extern const char *const HTTP_METHOD_PUT;
extern const char *const HTTP_METHOD_TRACE;


struct http_headers {
  uint32_t size;
};


struct http_request {
  uint32_t version_major;
  uint32_t version_minor;
  const char *method;
  struct uri uri;
  const char *uri_asterisk;
  const char *host;
  struct http_headers headers;
};


enum http_request_parse_status http_request_init(struct http_request *req);
enum http_request_parse_status http_request_destroy(struct http_request *req);
enum http_request_parse_status http_request_parse(struct http_request *req, struct lexer *lex);

#endif  // HTTP_H_
