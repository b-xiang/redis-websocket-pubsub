/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#include "http.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "uri.h"

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
static bool
parse_http_version(struct lexer *const lex) {
  uint32_t version_major, version_minor;

  // "HTTP"
  if (lexer_nremaining(lex) < 4 || lexer_strcmp(lex, "HTTP") != 0) {
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
  return true;

fail:
  return false;
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
static bool
parse_http_method(struct http_request *const req, struct lexer *const lex) {
  const size_t remaining = lexer_nremaining(lex);
  if (remaining >= 3) {
    if (lexer_strcmp(lex, "GET") == 0) {
      req->method = HTTP_METHOD_GET;
      lexer_consume(lex, 3);
      return true;
    }
    else if (lexer_strcmp(lex, "PUT") == 0) {
      req->method = HTTP_METHOD_PUT;
      lexer_consume(lex, 3);
      return true;
    }
  }
  if (remaining >= 4) {
    if (lexer_strcmp(lex, "HEAD") == 0) {
      req->method = HTTP_METHOD_HEAD;
      lexer_consume(lex, 4);
      return true;
    }
    else if (lexer_strcmp(lex, "POST") == 0) {
      req->method = HTTP_METHOD_POST;
      lexer_consume(lex, 4);
      return true;
    }
  }
  if (remaining >= 5 && lexer_strcmp(lex, "TRACE") == 0) {
    req->method = HTTP_METHOD_TRACE;
    lexer_consume(lex, 5);
    return true;
  }
  if (remaining >= 6 && lexer_strcmp(lex, "DELETE") == 0) {
    req->method = HTTP_METHOD_DELETE;
    lexer_consume(lex, 6);
    return true;
  }
  if (remaining >= 7) {
    if (lexer_strcmp(lex, "CONNECT") == 0) {
      req->method = HTTP_METHOD_CONNECT;
      lexer_consume(lex, 7);
      return true;
    }
    else if (lexer_strcmp(lex, "OPTIONS") == 0) {
      req->method = HTTP_METHOD_OPTIONS;
      lexer_consume(lex, 7);
      return true;
    }
  }
  return false;
}


/**
 * Request-URI = "*" | absoluteURI | abs_path | authority
 **/
static bool
parse_http_request_uri(struct http_request *const req, struct lexer *const lex) {
  (void)req;  // TODO
  (void)lex;  // TODO
  return false;
}


/**
 * Request-Line = Method SP Request-URI SP HTTP-Version CRLF
 **/
static bool
parse_http_line_request(struct http_request *const req, struct lexer *const lex) {
  // Method
  if (!parse_http_method(req, lex)) {
    goto fail;
  }

  // SP
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // Request-URI
  if (!parse_http_request_uri(req, lex)) {
    goto fail;
  }

  // SP
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // HTTP-Version
  if (!parse_http_version(lex)) {
    goto fail;
  }

  // CRLF
  if (lexer_nremaining(lex) < 2 || lexer_strcmp(lex, "\r\n") != 0) {
    goto fail;
  }
  lexer_consume(lex, 2);

  return true;

fail:
  return false;
}


/**
 * Request = Request-Line              ; Section 5.1
 *           *(( general-header        ; Section 4.5
 *            | request-header         ; Section 5.3
 *            | entity-header ) CRLF)  ; Section 7.1
 *           CRLF
 *           [ message-body ]          ; Section 4.3
 **/
static bool
parse_http_request(struct http_request *const req, struct lexer *const lex) {
  if (!parse_http_line_request(req, lex)) {
    return false;
  }

  return true;
}


// ================================================================================================
// Public API for `http_request`.
// ================================================================================================
bool
http_request_init(struct http_request *const req) {
  if (req == NULL) {
    return false;
  }

  memset(req, 0, sizeof(struct http_request));
  return true;
}


bool
http_request_destroy(struct http_request *const req) {
  (void)req;
  return true;
}


/**
 * https://tools.ietf.org/html/rfc2616#section-4
 **/
bool
http_request_parse(struct http_request *const req, struct lexer *const lex) {
  if (req == NULL || lex == NULL) {
    return false;
  }

  if (!parse_http_request(req, lex)) {
    return false;
  }

  return true;
}
