/**
 * The HTTP 1.1 protocol is defined in RFC2616
 * https://tools.ietf.org/html/rfc2616
 **/
#include "http.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "logging.h"

/*
https://tools.ietf.org/html/rfc2616#section-2.2

OCTET          = <any 8-bit sequence of data>
CHAR           = <any US-ASCII character (octets 0 - 127)>
UPALPHA        = <any US-ASCII uppercase letter "A".."Z">
LOALPHA        = <any US-ASCII lowercase letter "a".."z">
ALPHA          = UPALPHA | LOALPHA
DIGIT          = <any US-ASCII digit "0".."9">
CTL            = <any US-ASCII control character (octets 0 - 31) and DEL (127)>
CR             = <US-ASCII CR, carriage return (13)>
LF             = <US-ASCII LF, linefeed (10)>
SP             = <US-ASCII SP, space (32)>
HT             = <US-ASCII HT, horizontal-tab (9)>
<">            = <US-ASCII double-quote mark (34)>

CRLF           = CR LF

LWS            = [CRLF] 1*( SP | HT )

TEXT           = <any OCTET except CTLs, but including LWS>

token          = 1*<any CHAR except CTLs or separators>
separators     = "(" | ")" | "<" | ">" | "@" | "," | ";" | ":" | "\" | <"> | "/" | "[" | "]" | "?" | "=" | "{" | "}" | SP | HT

comment        = "(" *( ctext | quoted-pair | comment ) ")"
ctext          = <any TEXT excluding "(" and ")">

quoted-string  = ( <"> *(qdtext | quoted-pair ) <"> )
qdtext         = <any TEXT except <">>

quoted-pair    = "\" CHAR

message-header = field-name ":" [ field-value ]
field-name     = token
field-value    = *( field-content | LWS )
field-content  = <the OCTETs making up the field-value and consisting of either *TEXT or combinations of token, separators, and quoted-string>

generic-message = start-line *(message-header CRLF) CRLF [ message-body ]
*/

static const uint8_t CTYPE_TEXT      = 1 << 0;
static const uint8_t CTYPE_CHAR      = 1 << 1;
static const uint8_t CTYPE_CTL       = 1 << 2;
static const uint8_t CTYPE_SEPARATOR = 1 << 3;
static const uint8_t CTYPE_TOKEN     = 1 << 4;

static const uint8_t CTYPES[256] = {
  CTYPE_CTL | CTYPE_CHAR,  // 0x00
  CTYPE_CTL | CTYPE_CHAR,  // 0x01
  CTYPE_CTL | CTYPE_CHAR,  // 0x02
  CTYPE_CTL | CTYPE_CHAR,  // 0x03
  CTYPE_CTL | CTYPE_CHAR,  // 0x04
  CTYPE_CTL | CTYPE_CHAR,  // 0x05
  CTYPE_CTL | CTYPE_CHAR,  // 0x06
  CTYPE_CTL | CTYPE_CHAR,  // 0x07
  CTYPE_CTL | CTYPE_CHAR,  // 0x08
  CTYPE_CTL | CTYPE_CHAR | CTYPE_TEXT | CTYPE_SEPARATOR,  // 0x09
  CTYPE_CTL | CTYPE_CHAR,  // 0x0a
  CTYPE_CTL | CTYPE_CHAR,  // 0x0b
  CTYPE_CTL | CTYPE_CHAR,  // 0x0c
  CTYPE_CTL | CTYPE_CHAR,  // 0x0d
  CTYPE_CTL | CTYPE_CHAR,  // 0x0e
  CTYPE_CTL | CTYPE_CHAR,  // 0x0f
  CTYPE_CTL | CTYPE_CHAR,  // 0x10
  CTYPE_CTL | CTYPE_CHAR,  // 0x11
  CTYPE_CTL | CTYPE_CHAR,  // 0x12
  CTYPE_CTL | CTYPE_CHAR,  // 0x13
  CTYPE_CTL | CTYPE_CHAR,  // 0x14
  CTYPE_CTL | CTYPE_CHAR,  // 0x15
  CTYPE_CTL | CTYPE_CHAR,  // 0x16
  CTYPE_CTL | CTYPE_CHAR,  // 0x17
  CTYPE_CTL | CTYPE_CHAR,  // 0x18
  CTYPE_CTL | CTYPE_CHAR,  // 0x19
  CTYPE_CTL | CTYPE_CHAR,  // 0x1a
  CTYPE_CTL | CTYPE_CHAR,  // 0x1b
  CTYPE_CTL | CTYPE_CHAR,  // 0x1c
  CTYPE_CTL | CTYPE_CHAR,  // 0x1d
  CTYPE_CTL | CTYPE_CHAR,  // 0x1e
  CTYPE_CTL | CTYPE_CHAR,  // 0x1f
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // " "
  CTYPE_TEXT | CTYPE_CHAR,  // "!"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // """
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "#"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "$"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "%"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "&"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "'"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "("
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // ")"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "*"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "+"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // ","
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "-"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "."
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "/"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "0"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "1"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "2"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "3"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "4"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "5"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "6"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "7"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "8"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "9"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // ":"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // ";"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "<"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "="
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // ">"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "?"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "@"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "A"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "B"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "C"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "D"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "E"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "F"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "G"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "H"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "I"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "J"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "K"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "L"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "M"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "N"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "O"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "P"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "Q"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "R"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "S"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "T"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "U"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "V"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "W"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "X"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "Y"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "Z"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "["
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "\"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "]"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "^"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "_"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "`"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "a"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "b"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "c"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "d"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "e"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "f"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "g"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "h"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "i"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "j"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "k"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "l"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "m"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "n"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "o"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "p"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "q"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "r"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "s"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "t"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "u"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "v"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "w"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "x"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "y"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "z"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "{"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "|"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_SEPARATOR,  // "}"
  CTYPE_TEXT | CTYPE_CHAR | CTYPE_TOKEN,  // "~"
  CTYPE_CTL | CTYPE_CHAR,  // 0x7f
  CTYPE_TEXT,  // 0x80
  CTYPE_TEXT,  // 0x81
  CTYPE_TEXT,  // 0x82
  CTYPE_TEXT,  // 0x83
  CTYPE_TEXT,  // 0x84
  CTYPE_TEXT,  // 0x85
  CTYPE_TEXT,  // 0x86
  CTYPE_TEXT,  // 0x87
  CTYPE_TEXT,  // 0x88
  CTYPE_TEXT,  // 0x89
  CTYPE_TEXT,  // 0x8a
  CTYPE_TEXT,  // 0x8b
  CTYPE_TEXT,  // 0x8c
  CTYPE_TEXT,  // 0x8d
  CTYPE_TEXT,  // 0x8e
  CTYPE_TEXT,  // 0x8f
  CTYPE_TEXT,  // 0x90
  CTYPE_TEXT,  // 0x91
  CTYPE_TEXT,  // 0x92
  CTYPE_TEXT,  // 0x93
  CTYPE_TEXT,  // 0x94
  CTYPE_TEXT,  // 0x95
  CTYPE_TEXT,  // 0x96
  CTYPE_TEXT,  // 0x97
  CTYPE_TEXT,  // 0x98
  CTYPE_TEXT,  // 0x99
  CTYPE_TEXT,  // 0x9a
  CTYPE_TEXT,  // 0x9b
  CTYPE_TEXT,  // 0x9c
  CTYPE_TEXT,  // 0x9d
  CTYPE_TEXT,  // 0x9e
  CTYPE_TEXT,  // 0x9f
  CTYPE_TEXT,  // 0xa0
  CTYPE_TEXT,  // 0xa1
  CTYPE_TEXT,  // 0xa2
  CTYPE_TEXT,  // 0xa3
  CTYPE_TEXT,  // 0xa4
  CTYPE_TEXT,  // 0xa5
  CTYPE_TEXT,  // 0xa6
  CTYPE_TEXT,  // 0xa7
  CTYPE_TEXT,  // 0xa8
  CTYPE_TEXT,  // 0xa9
  CTYPE_TEXT,  // 0xaa
  CTYPE_TEXT,  // 0xab
  CTYPE_TEXT,  // 0xac
  CTYPE_TEXT,  // 0xad
  CTYPE_TEXT,  // 0xae
  CTYPE_TEXT,  // 0xaf
  CTYPE_TEXT,  // 0xb0
  CTYPE_TEXT,  // 0xb1
  CTYPE_TEXT,  // 0xb2
  CTYPE_TEXT,  // 0xb3
  CTYPE_TEXT,  // 0xb4
  CTYPE_TEXT,  // 0xb5
  CTYPE_TEXT,  // 0xb6
  CTYPE_TEXT,  // 0xb7
  CTYPE_TEXT,  // 0xb8
  CTYPE_TEXT,  // 0xb9
  CTYPE_TEXT,  // 0xba
  CTYPE_TEXT,  // 0xbb
  CTYPE_TEXT,  // 0xbc
  CTYPE_TEXT,  // 0xbd
  CTYPE_TEXT,  // 0xbe
  CTYPE_TEXT,  // 0xbf
  CTYPE_TEXT,  // 0xc0
  CTYPE_TEXT,  // 0xc1
  CTYPE_TEXT,  // 0xc2
  CTYPE_TEXT,  // 0xc3
  CTYPE_TEXT,  // 0xc4
  CTYPE_TEXT,  // 0xc5
  CTYPE_TEXT,  // 0xc6
  CTYPE_TEXT,  // 0xc7
  CTYPE_TEXT,  // 0xc8
  CTYPE_TEXT,  // 0xc9
  CTYPE_TEXT,  // 0xca
  CTYPE_TEXT,  // 0xcb
  CTYPE_TEXT,  // 0xcc
  CTYPE_TEXT,  // 0xcd
  CTYPE_TEXT,  // 0xce
  CTYPE_TEXT,  // 0xcf
  CTYPE_TEXT,  // 0xd0
  CTYPE_TEXT,  // 0xd1
  CTYPE_TEXT,  // 0xd2
  CTYPE_TEXT,  // 0xd3
  CTYPE_TEXT,  // 0xd4
  CTYPE_TEXT,  // 0xd5
  CTYPE_TEXT,  // 0xd6
  CTYPE_TEXT,  // 0xd7
  CTYPE_TEXT,  // 0xd8
  CTYPE_TEXT,  // 0xd9
  CTYPE_TEXT,  // 0xda
  CTYPE_TEXT,  // 0xdb
  CTYPE_TEXT,  // 0xdc
  CTYPE_TEXT,  // 0xdd
  CTYPE_TEXT,  // 0xde
  CTYPE_TEXT,  // 0xdf
  CTYPE_TEXT,  // 0xe0
  CTYPE_TEXT,  // 0xe1
  CTYPE_TEXT,  // 0xe2
  CTYPE_TEXT,  // 0xe3
  CTYPE_TEXT,  // 0xe4
  CTYPE_TEXT,  // 0xe5
  CTYPE_TEXT,  // 0xe6
  CTYPE_TEXT,  // 0xe7
  CTYPE_TEXT,  // 0xe8
  CTYPE_TEXT,  // 0xe9
  CTYPE_TEXT,  // 0xea
  CTYPE_TEXT,  // 0xeb
  CTYPE_TEXT,  // 0xec
  CTYPE_TEXT,  // 0xed
  CTYPE_TEXT,  // 0xee
  CTYPE_TEXT,  // 0xef
  CTYPE_TEXT,  // 0xf0
  CTYPE_TEXT,  // 0xf1
  CTYPE_TEXT,  // 0xf2
  CTYPE_TEXT,  // 0xf3
  CTYPE_TEXT,  // 0xf4
  CTYPE_TEXT,  // 0xf5
  CTYPE_TEXT,  // 0xf6
  CTYPE_TEXT,  // 0xf7
  CTYPE_TEXT,  // 0xf8
  CTYPE_TEXT,  // 0xf9
  CTYPE_TEXT,  // 0xfa
  CTYPE_TEXT,  // 0xfb
  CTYPE_TEXT,  // 0xfc
  CTYPE_TEXT,  // 0xfd
  CTYPE_TEXT,  // 0xfe
  CTYPE_TEXT,  // 0xff
};

#define HAS_CTYPE(c, mask) ((CTYPES[c & 0xff] & mask) != 0)

const char *const HTTP_METHOD_CONNECT = "CONNECT";
const char *const HTTP_METHOD_DELETE = "DELETE";
const char *const HTTP_METHOD_GET = "GET";
const char *const HTTP_METHOD_HEAD = "HEAD";
const char *const HTTP_METHOD_OPTIONS = "OPTIONS";
const char *const HTTP_METHOD_POST = "POST";
const char *const HTTP_METHOD_PUT = "PUT";
const char *const HTTP_METHOD_TRACE = "TRACE";

const char *const HTTP_REQUEST_URI_ASTERISK = "*";


/**
 * HTTP-Version = "HTTP" "/" 1*DIGIT "." 1*DIGIT
 **/
static enum status
parse_http_version(struct http_request *const req, struct lexer *const lex) {
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
  if (!lexer_consume_uint32(lex, &req->version_major)) {
    goto fail;
  }

  // "."
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != '.') {
    goto fail;
  }
  lexer_consume(lex, 1);
  lexer_consume_lws(lex);

  // 1*DIGIT
  if (!lexer_consume_uint32(lex, &req->version_minor)) {
    goto fail;
  }

  return STATUS_OK;

fail:
  return STATUS_BAD;
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
static enum status
parse_http_method(struct http_request *const req, struct lexer *const lex) {
  const size_t remaining = lexer_nremaining(lex);
  if (remaining >= 3) {
    if (lexer_memcmp(lex, "GET", 3) == 0) {
      req->method = HTTP_METHOD_GET;
      lexer_consume(lex, 3);
      return STATUS_OK;
    }
    else if (lexer_memcmp(lex, "PUT", 3) == 0) {
      req->method = HTTP_METHOD_PUT;
      lexer_consume(lex, 3);
      return STATUS_OK;
    }
  }
  if (remaining >= 4) {
    if (lexer_memcmp(lex, "HEAD", 4) == 0) {
      req->method = HTTP_METHOD_HEAD;
      lexer_consume(lex, 4);
      return STATUS_OK;
    }
    else if (lexer_memcmp(lex, "POST", 4) == 0) {
      req->method = HTTP_METHOD_POST;
      lexer_consume(lex, 4);
      return STATUS_OK;
    }
  }
  if (remaining >= 5 && lexer_memcmp(lex, "TRACE", 5) == 0) {
    req->method = HTTP_METHOD_TRACE;
    lexer_consume(lex, 5);
    return STATUS_OK;
  }
  if (remaining >= 6 && lexer_memcmp(lex, "DELETE", 5) == 0) {
    req->method = HTTP_METHOD_DELETE;
    lexer_consume(lex, 6);
    return STATUS_OK;
  }
  if (remaining >= 7) {
    if (lexer_memcmp(lex, "CONNECT", 7) == 0) {
      req->method = HTTP_METHOD_CONNECT;
      lexer_consume(lex, 7);
      return STATUS_OK;
    }
    else if (lexer_memcmp(lex, "OPTIONS", 7) == 0) {
      req->method = HTTP_METHOD_OPTIONS;
      lexer_consume(lex, 7);
      return STATUS_OK;
    }
  }
  return STATUS_BAD;
}


/**
 * Request-URI = "*" | absoluteURI | abs_path | authority
 **/
static enum status
parse_http_request_uri(struct http_request *const req, struct lexer *const lex) {
  if (lexer_nremaining(lex) == 0) {
    return STATUS_BAD;
  }

  if (lexer_peek(lex) == '*') {
    req->uri_asterisk = HTTP_REQUEST_URI_ASTERISK;
    return STATUS_OK;
  }

  enum status status = uri_parse_absolute_uri(&req->uri, lex);
  if (status == STATUS_BAD) {
    status = uri_parse_abs_path(&req->uri, lex);
    if (status == STATUS_BAD) {
      status = uri_parse_authority(&req->uri, lex);
    }
  }
  return status;
}


/**
 * Request-Line = Method SP Request-URI SP HTTP-Version CRLF
 **/
static enum status
parse_http_line_request(struct http_request *const req, struct lexer *const lex) {
  // Method
  if (!parse_http_method(req, lex)) {
    goto fail;
  }

  // SP
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // Request-URI
  if (!parse_http_request_uri(req, lex)) {
    goto fail;
  }

  // SP
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ' ') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // HTTP-Version
  if (!parse_http_version(req, lex)) {
    goto fail;
  }

  // CRLF
  if (lexer_nremaining(lex) < 2 || lexer_memcmp(lex, "\r\n", 2) != 0) {
    goto fail;
  }
  lexer_consume(lex, 2);

  return STATUS_OK;

fail:
  return STATUS_BAD;
}


/**
 * Request = Request-Line              ; Section 5.1
 *           *(( general-header        ; Section 4.5
 *            | request-header         ; Section 5.3
 *            | entity-header ) CRLF)  ; Section 7.1
 *           CRLF
 *           [ message-body ]          ; Section 4.3
 *
 * message-header = field-name ":" [ field-value ]
 * field-name     = token
 * field-value    = *( field-content | LWS )
 * field-content  = <the OCTETs making up the field-value and consisting of either *TEXT or combinations of token, separators, and quoted-string>
 *
 * generic-message = start-line *(message-header CRLF) CRLF [ message-body ]
 **/
static enum status
parse_http_request(struct http_request *const req, struct lexer *const lex) {
  // Request-Line
  enum status status = parse_http_line_request(req, lex);
  if (status != STATUS_OK) {
    return status;
  }

  // *(message-header CRLF)
  while (1) {
    if (lexer_nremaining(lex) == 0) {
      return STATUS_BAD;
    }

    // field-name
    const char *name_start = lexer_upto(lex);
    while (1) {
      if (lexer_nremaining(lex) == 0) {
        return STATUS_BAD;
      }
      else if (!HAS_CTYPE(lexer_peek(lex), CTYPE_TOKEN)) {
        break;
      }
      lexer_consume(lex, 1);
    }
    const char *name_end = lexer_upto(lex);
    const size_t name_nbytes = name_end - name_start;
    if (name_nbytes == 0) {
      break;
    }

    // ":"
    if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ':') {
      return STATUS_BAD;
    }
    lexer_consume(lex, 1);

    // LWS
    if (!lexer_consume_lws(lex)) {
      return STATUS_BAD;
    }

    // field-value
    const char *value_start = lexer_upto(lex);
    while (1) {
      if (lexer_nremaining(lex) == 0) {
        return STATUS_BAD;
      }
      else if (!HAS_CTYPE(lexer_peek(lex), CTYPE_TEXT)) {
        break;
      }
      lexer_consume(lex, 1);
    }
    const char *value_end = lexer_upto(lex);
    const size_t value_nbytes = value_end - value_start;

    // CRLF
    if (lexer_nremaining(lex) < 2 || lexer_memcmp(lex, "\r\n", 2) != 0) {
      return STATUS_BAD;
    }
    lexer_consume(lex, 2);

    // Add the header to the request object.
    status = http_request_add_header(req, name_start, name_nbytes, value_start, value_nbytes);
    if (status != STATUS_OK) {
      return status;
    }
  }

  // CRLF
  if (lexer_nremaining(lex) < 2 || lexer_memcmp(lex, "\r\n", 2) != 0) {
    return STATUS_BAD;
  }
  lexer_consume(lex, 2);

  // TODO

  return STATUS_OK;
}


// ================================================================================================
// Public API for `http_request`.
// ================================================================================================
struct http_request *
http_request_init(void) {
  struct http_request *const req = malloc(sizeof(struct http_request));
  if (req == NULL) {
    return NULL;
  }

  memset(req, 0, sizeof(struct http_request));
  enum status status = uri_init(&req->uri);
  if (status != STATUS_OK) {
    free(req);
    return NULL;
  }

  return req;
}


enum status
http_request_destroy(struct http_request *const req) {
  if (req == NULL) {
    return STATUS_OK;
  }

  // Destroy the URI.
  enum status status = uri_destroy(&req->uri);

  // Destroy the headers.
  for (struct http_request_header *header = req->header; header != NULL; ) {
    struct http_request_header *const next = header->next;
    free(header->name);
    free(header->value);
    free(header);
    header = next;
  }

  // Destroy the request.
  free(req);

  return status;
}


/**
 * https://tools.ietf.org/html/rfc2616#section-4
 **/
enum status
http_request_parse(struct http_request *const req, struct lexer *const lex) {
  const struct http_request_header *header = NULL;
  if (req == NULL || lex == NULL) {
    return STATUS_EINVAL;
  }

  enum status status = parse_http_request(req, lex);
  if (status != STATUS_OK) {
    return status;
  }

  for (const struct http_request_header *header = req->header; header != NULL; header = header->next) {
    DEBUG("http_request_parse", "Request header '%s' => '%s'\n", header->name, header->value);
  }

  // Ensure either the URI has a netloc, or the HOST header exists (or both).
  if (req->uri.netloc != NULL) {
    req->host = req->uri.netloc;
  }
  header = http_request_find_header(req, "HOST");
  if (header != NULL) {
    if (req->host != NULL) {
      if (strcmp(header->value, req->host) != 0) {
        INFO("http_request_parse", "URI netloc '%s' != HOST header '%s'. Aborting connection.\n", header->value, req->host);
        return STATUS_BAD;
      }
    }
    else {
      req->host = header->value;
    }
  }
  if (req->host == NULL) {
    INFO0("http_request_parse", "Request has no host information. Aborting connection.\n");
    return STATUS_BAD;
  }

  return STATUS_OK;
}


enum status
http_request_add_header(struct http_request *const req, const char *const name, const size_t name_nbytes, const char *const value, const size_t value_nbytes) {
  char *name_copy = NULL, *value_copy = NULL;

  if (req == NULL || name == NULL || value == NULL) {
    return STATUS_EINVAL;
  }

  // Create a local copy of the name, converted to uppercase.
  name_copy = malloc(name_nbytes + 1);
  if (name_copy == NULL) {
    goto fail;
  }
  for (size_t i = 0; i != name_nbytes; ++i) {
    name_copy[i] = toupper(name[i]);
  }
  name_copy[name_nbytes] = '\0';

  // Create a local copy of the value.
  value_copy = malloc(value_nbytes + 1);
  if (value_copy == NULL) {
    goto fail;
  }
  memcpy(value_copy, value, value_nbytes);
  value_copy[value_nbytes] = '\0';

  // See if a header with the name already exists.
  struct http_request_header *header;
  for (header = req->header; header != NULL; header = header->next) {
    if (strcmp(header->name, name_copy) == 0) {
      break;
    }
  }

  // Replace or create the header.
  if (header == NULL) {
    header = malloc(sizeof(struct http_request_header));
    if (header == NULL) {
      goto fail;
    }
    header->name = name_copy;
    header->value = value_copy;
    header->next = req->header;
    req->header = header;
  }
  else {
    free(name_copy);
    free(header->value);
    header->value = value_copy;
  }

  return STATUS_OK;

fail:
  free(name_copy);
  free(value_copy);
  return STATUS_ENOMEM;
}


struct http_request_header *
http_request_find_header(const struct http_request *const req, const char *const name_upper) {
  if (req == NULL || name_upper == NULL) {
    return NULL;
  }

  for (struct http_request_header *header = req->header; header != NULL; header = header->next) {
    if (strcmp(header->name, name_upper) == 0) {
      return header;
    }
  }
  return NULL;
}
