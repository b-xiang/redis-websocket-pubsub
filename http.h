/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#ifndef HTTP_H_
#define HTTP_H_

#include <stdbool.h>
#include <stdint.h>

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


struct http_headers {
  uint32_t size;
};


struct http_request {
  uint32_t version_major;
  uint32_t version_minor;
  const char *method;
  struct http_headers headers;
};


bool http_request_init(struct http_request *req);
bool http_request_destroy(struct http_request *req);
bool http_request_parse(struct http_request *req, struct lexer *lex);

#endif  // HTTP_H_
