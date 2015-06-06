/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#include "http.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

const char *const HTTP_METHOD_CONNECT = "CONNECT";
const char *const HTTP_METHOD_DELETE = "DELETE";
const char *const HTTP_METHOD_GET = "GET";
const char *const HTTP_METHOD_HEAD = "HEAD";
const char *const HTTP_METHOD_OPTIONS = "OPTIONS";
const char *const HTTP_METHOD_POST = "POST";
const char *const HTTP_METHOD_PUT = "PUT";
const char *const HTTP_METHOD_TRACE = "TRACE";


/**
 * HTTP-Version = "HTTP" "/" 1*DIGIT "." 1*DIGIT
 **/
static enum http_request_parse_status
parse_http_version(struct lexer *const lex) {
  uint32_t version_major, version_minor;

  // "HTTP"
  if (lexer_nremaining(lex) < 4 || lexer_memcmp(lex, "HTTP", 4) != 0) {
    goto fail;
  }
  lexer_consume(lex, 4);
  lexer_consume_lws(lex);

  // "/"
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != '/') {
    goto fail;
  }
  lexer_consume(lex, 1);
  lexer_consume_lws(lex);

  // 1*DIGIT
  if (!lexer_consume_uint32(lex, &version_major)) {
    goto fail;
  }

  // "."
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != '.') {
    goto fail;
  }
  lexer_consume(lex, 1);
  lexer_consume_lws(lex);

  // 1*DIGIT
  if (!lexer_consume_uint32(lex, &version_minor)) {
    goto fail;
  }

  fprintf(stderr, "HTTP-Version: major=%u minor=%u\n", version_major, version_minor);
  return HTTP_REQUEST_PARSE_OK;

fail:
  return HTTP_REQUEST_PARSE_BAD;
}


/**
 * Method         = "OPTIONS"                ; Section 9.2
 *                | "GET"                    ; Section 9.3
 *                | "HEAD"                   ; Section 9.4
 *                | "POST"                   ; Section 9.5
 *                | "PUT"                    ; Section 9.6
 *                | "DELETE"                 ; Section 9.7
 *                | "TRACE"                  ; Section 9.8
 *                | "CONNECT"                ; Section 9.9
 *                | extension-method
 * extension-method = token
 **/
static enum http_request_parse_status
parse_http_method(struct http_request *const req, struct lexer *const lex) {
  const size_t remaining = lexer_nremaining(lex);
  if (remaining >= 3) {
    if (lexer_memcmp(lex, "GET", 3) == 0) {
      req->method = HTTP_METHOD_GET;
      lexer_consume(lex, 3);
      return HTTP_REQUEST_PARSE_OK;
    }
    else if (lexer_memcmp(lex, "PUT", 3) == 0) {
      req->method = HTTP_METHOD_PUT;
      lexer_consume(lex, 3);
      return HTTP_REQUEST_PARSE_OK;
    }
  }
  if (remaining >= 4) {
    if (lexer_memcmp(lex, "HEAD", 4) == 0) {
      req->method = HTTP_METHOD_HEAD;
      lexer_consume(lex, 4);
      return HTTP_REQUEST_PARSE_OK;
    }
    else if (lexer_memcmp(lex, "POST", 4) == 0) {
      req->method = HTTP_METHOD_POST;
      lexer_consume(lex, 4);
      return HTTP_REQUEST_PARSE_OK;
    }
  }
  if (remaining >= 5 && lexer_memcmp(lex, "TRACE", 5) == 0) {
    req->method = HTTP_METHOD_TRACE;
    lexer_consume(lex, 5);
    return HTTP_REQUEST_PARSE_OK;
  }
  if (remaining >= 6 && lexer_memcmp(lex, "DELETE", 5) == 0) {
    req->method = HTTP_METHOD_DELETE;
    lexer_consume(lex, 6);
    return HTTP_REQUEST_PARSE_OK;
  }
  if (remaining >= 7) {
    if (lexer_memcmp(lex, "CONNECT", 7) == 0) {
      req->method = HTTP_METHOD_CONNECT;
      lexer_consume(lex, 7);
      return HTTP_REQUEST_PARSE_OK;
    }
    else if (lexer_memcmp(lex, "OPTIONS", 7) == 0) {
      req->method = HTTP_METHOD_OPTIONS;
      lexer_consume(lex, 7);
      return HTTP_REQUEST_PARSE_OK;
    }
  }
  return HTTP_REQUEST_PARSE_BAD;
}


/**
 * Request-URI = "*" | absoluteURI | abs_path | authority
 **/
static enum http_request_parse_status
parse_http_request_uri(struct http_request *const req, struct lexer *const lex) {
  if (lexer_nremaining(lex) == 0) {
    return HTTP_REQUEST_PARSE_BAD;
  }

  if (lexer_peek(lex) == '*') {
    req->uri = malloc(2);
    if (req->uri == NULL) {
      return HTTP_REQUEST_PARSE_ENOMEM;
    }
    req->uri[0] = '*';
    req->uri[1] = '\0';
    return HTTP_REQUEST_PARSE_OK;
  }

  struct uri uri;
  enum uri_parse_status status = uri_init(&uri);
  if (status == URI_PARSE_ENOMEM) {
    return HTTP_REQUEST_PARSE_ENOMEM;
  }

  status = uri_parse_absolute_uri(&uri, lex);
  if (status == URI_PARSE_ENOMEM) {
    return HTTP_REQUEST_PARSE_ENOMEM;
  }
  else if (status == URI_PARSE_BAD) {
    status = uri_parse_abs_path(&uri, lex);
  }

  if (status == URI_PARSE_ENOMEM) {
    return HTTP_REQUEST_PARSE_ENOMEM;
  }
  else if (status == URI_PARSE_BAD) {
    uri_parse_authority(&uri, lex);
  }

  if (status == URI_PARSE_ENOMEM) {
    return HTTP_REQUEST_PARSE_ENOMEM;
  }
  if (uri_destroy(&uri) == URI_PARSE_ENOMEM) {
    return HTTP_REQUEST_PARSE_ENOMEM;
  }

  return (status == URI_PARSE_OK) ? HTTP_REQUEST_PARSE_OK : HTTP_REQUEST_PARSE_BAD;
}


/**
 * Request-Line = Method SP Request-URI SP HTTP-Version CRLF
 **/
static enum http_request_parse_status
parse_http_line_request(struct http_request *const req, struct lexer *const lex) {
  // Method
  fprintf(stderr, "[parse_http_line_request] before parse_http_method\n");
  if (!parse_http_method(req, lex)) {
    goto fail;
  }

  // SP
  fprintf(stderr, "[parse_http_line_request] before SP\n");
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // Request-URI
  fprintf(stderr, "[parse_http_line_request] before request-uri\n");
  if (!parse_http_request_uri(req, lex)) {
    goto fail;
  }

  // SP
  fprintf(stderr, "[parse_http_line_request] before SP '%c'\n", lexer_peek(lex));
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // HTTP-Version
  fprintf(stderr, "[parse_http_line_request] before HTTP-Version\n");
  if (!parse_http_version(lex)) {
    goto fail;
  }

  // CRLF
  fprintf(stderr, "[parse_http_line_request] before CRLF\n");
  if (lexer_nremaining(lex) < 2 || lexer_memcmp(lex, "\r\n", 2) != 0) {
    goto fail;
  }
  lexer_consume(lex, 2);

  return HTTP_REQUEST_PARSE_OK;

fail:
  return HTTP_REQUEST_PARSE_BAD;
}


/**
 * Request = Request-Line              ; Section 5.1
 *           *(( general-header        ; Section 4.5
 *            | request-header         ; Section 5.3
 *            | entity-header ) CRLF)  ; Section 7.1
 *           CRLF
 *           [ message-body ]          ; Section 4.3
 **/
static enum http_request_parse_status
parse_http_request(struct http_request *const req, struct lexer *const lex) {
  enum http_request_parse_status status = parse_http_line_request(req, lex);
  if (status != HTTP_REQUEST_PARSE_OK) {
    return status;
  }

  // TODO

  return HTTP_REQUEST_PARSE_OK;
}


// ================================================================================================
// Public API for `http_request`.
// ================================================================================================
enum http_request_parse_status
http_request_init(struct http_request *const req) {
  if (req == NULL) {
    return HTTP_REQUEST_PARSE_BAD;
  }

  memset(req, 0, sizeof(struct http_request));
  return HTTP_REQUEST_PARSE_OK;
}


enum http_request_parse_status
http_request_destroy(struct http_request *const req) {
  if (req == NULL) {
    return HTTP_REQUEST_PARSE_BAD;
  }

  free(req->uri);
  return HTTP_REQUEST_PARSE_OK;
}


/**
 * https://tools.ietf.org/html/rfc2616#section-4
 **/
enum http_request_parse_status
http_request_parse(struct http_request *const req, struct lexer *const lex) {
  if (req == NULL || lex == NULL) {
    return HTTP_REQUEST_PARSE_BAD;
  }

  enum http_request_parse_status status = parse_http_request(req, lex);
  if (status != HTTP_REQUEST_PARSE_OK) {
    return status;
  }

  return HTTP_REQUEST_PARSE_OK;
}
