/**
 * The URI format is defined in RFC2396
 * https://tools.ietf.org/html/rfc2396
 **/
#include "uri.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

#define CTYPE_NONE                 ((uint16_t)(0))
#define CTYPE_ALPHA                ((uint16_t)(1 <<  0))
#define CTYPE_DIGIT                ((uint16_t)(1 <<  1))
#define CTYPE_HEX                  ((uint16_t)(1 <<  2))
#define CTYPE_MARK                 ((uint16_t)(1 <<  3))
#define CTYPE_RESERVED             ((uint16_t)(1 <<  4))
#define CTYPE_PCHAR_EXTRA          ((uint16_t)(1 <<  5))
#define CTYPE_USERINFO_EXTRA       ((uint16_t)(1 <<  6))
#define CTYPE_REG_NAME_EXTRA       ((uint16_t)(1 <<  7))
#define CTYPE_SCHEME_EXTRA         ((uint16_t)(1 <<  8))
#define CTYPE_REL_SEGMENT_EXTRA    ((uint16_t)(1 <<  9))
#define CTYPE_URIC_NO_SLASH_EXTRA  ((uint16_t)(1 << 10))

#define CTYPE_ALNUM          ((uint16_t)(CTYPE_ALPHA | CTYPE_DIGIT))
#define CTYPE_UNRESERVED     ((uint16_t)(CTYPE_ALNUM | CTYPE_MARK))
#define CTYPE_URIC           ((uint16_t)(CTYPE_RESERVED | CTYPE_UNRESERVED))
#define CTYPE_PCHAR          ((uint16_t)(CTYPE_UNRESERVED | CTYPE_PCHAR_EXTRA))
#define CTYPE_USERINFO       ((uint16_t)(CTYPE_UNRESERVED | CTYPE_USERINFO_EXTRA))
#define CTYPE_REG_NAME       ((uint16_t)(CTYPE_UNRESERVED | CTYPE_REG_NAME_EXTRA))
#define CTYPE_SCHEME         ((uint16_t)(CTYPE_ALNUM | CTYPE_SCHEME_EXTRA))
#define CTYPE_REL_SEGMENT    ((uint16_t)(CTYPE_UNRESERVED | CTYPE_REL_SEGMENT_EXTRA))
#define CTYPE_URIC_NO_SLASH  ((uint16_t)(CTYPE_UNRESERVED | CTYPE_URIC_NO_SLASH_EXTRA))

static const uint16_t CTYPES[128] = {
  CTYPE_NONE,  // 0x00
  CTYPE_NONE,  // 0x01
  CTYPE_NONE,  // 0x02
  CTYPE_NONE,  // 0x03
  CTYPE_NONE,  // 0x04
  CTYPE_NONE,  // 0x05
  CTYPE_NONE,  // 0x06
  CTYPE_NONE,  // 0x07
  CTYPE_NONE,  // 0x08
  CTYPE_NONE,  // 0x09
  CTYPE_NONE,  // 0x0a
  CTYPE_NONE,  // 0x0b
  CTYPE_NONE,  // 0x0c
  CTYPE_NONE,  // 0x0d
  CTYPE_NONE,  // 0x0e
  CTYPE_NONE,  // 0x0f
  CTYPE_NONE,  // 0x10
  CTYPE_NONE,  // 0x11
  CTYPE_NONE,  // 0x12
  CTYPE_NONE,  // 0x13
  CTYPE_NONE,  // 0x14
  CTYPE_NONE,  // 0x15
  CTYPE_NONE,  // 0x16
  CTYPE_NONE,  // 0x17
  CTYPE_NONE,  // 0x18
  CTYPE_NONE,  // 0x19
  CTYPE_NONE,  // 0x1a
  CTYPE_NONE,  // 0x1b
  CTYPE_NONE,  // 0x1c
  CTYPE_NONE,  // 0x1d
  CTYPE_NONE,  // 0x1e
  CTYPE_NONE,  // 0x1f
  CTYPE_NONE,  // " "
  CTYPE_MARK,  // "!"
  CTYPE_NONE,  // """
  CTYPE_NONE,  // "#"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // "$"
  CTYPE_NONE,  // "%"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // "&"
  CTYPE_MARK,  // "'"
  CTYPE_MARK,  // "("
  CTYPE_MARK,  // ")"
  CTYPE_MARK,  // "*"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_SCHEME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // "+"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // ","
  CTYPE_MARK | CTYPE_SCHEME_EXTRA,  // "-"
  CTYPE_MARK | CTYPE_SCHEME_EXTRA,  // "."
  CTYPE_RESERVED,  // "/"
  CTYPE_DIGIT | CTYPE_HEX,  // "0"
  CTYPE_DIGIT | CTYPE_HEX,  // "1"
  CTYPE_DIGIT | CTYPE_HEX,  // "2"
  CTYPE_DIGIT | CTYPE_HEX,  // "3"
  CTYPE_DIGIT | CTYPE_HEX,  // "4"
  CTYPE_DIGIT | CTYPE_HEX,  // "5"
  CTYPE_DIGIT | CTYPE_HEX,  // "6"
  CTYPE_DIGIT | CTYPE_HEX,  // "7"
  CTYPE_DIGIT | CTYPE_HEX,  // "8"
  CTYPE_DIGIT | CTYPE_HEX,  // "9"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // ":"
  CTYPE_RESERVED | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // ";"
  CTYPE_NONE,  // "<"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_USERINFO_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // "="
  CTYPE_NONE,  // ">"
  CTYPE_RESERVED | CTYPE_URIC_NO_SLASH_EXTRA,  // "?"
  CTYPE_RESERVED | CTYPE_PCHAR_EXTRA | CTYPE_REG_NAME_EXTRA | CTYPE_REL_SEGMENT_EXTRA | CTYPE_URIC_NO_SLASH_EXTRA,  // "@"
  CTYPE_ALPHA | CTYPE_HEX,  // "A"
  CTYPE_ALPHA | CTYPE_HEX,  // "B"
  CTYPE_ALPHA | CTYPE_HEX,  // "C"
  CTYPE_ALPHA | CTYPE_HEX,  // "D"
  CTYPE_ALPHA | CTYPE_HEX,  // "E"
  CTYPE_ALPHA | CTYPE_HEX,  // "F"
  CTYPE_ALPHA,  // "G"
  CTYPE_ALPHA,  // "H"
  CTYPE_ALPHA,  // "I"
  CTYPE_ALPHA,  // "J"
  CTYPE_ALPHA,  // "K"
  CTYPE_ALPHA,  // "L"
  CTYPE_ALPHA,  // "M"
  CTYPE_ALPHA,  // "N"
  CTYPE_ALPHA,  // "O"
  CTYPE_ALPHA,  // "P"
  CTYPE_ALPHA,  // "Q"
  CTYPE_ALPHA,  // "R"
  CTYPE_ALPHA,  // "S"
  CTYPE_ALPHA,  // "T"
  CTYPE_ALPHA,  // "U"
  CTYPE_ALPHA,  // "V"
  CTYPE_ALPHA,  // "W"
  CTYPE_ALPHA,  // "X"
  CTYPE_ALPHA,  // "Y"
  CTYPE_ALPHA,  // "Z"
  CTYPE_NONE,  // "["
  CTYPE_NONE,  // "\"
  CTYPE_NONE,  // "]"
  CTYPE_NONE,  // "^"
  CTYPE_MARK,  // "_"
  CTYPE_NONE,  // "`"
  CTYPE_ALPHA | CTYPE_HEX,  // "a"
  CTYPE_ALPHA | CTYPE_HEX,  // "b"
  CTYPE_ALPHA | CTYPE_HEX,  // "c"
  CTYPE_ALPHA | CTYPE_HEX,  // "d"
  CTYPE_ALPHA | CTYPE_HEX,  // "e"
  CTYPE_ALPHA | CTYPE_HEX,  // "f"
  CTYPE_ALPHA,  // "g"
  CTYPE_ALPHA,  // "h"
  CTYPE_ALPHA,  // "i"
  CTYPE_ALPHA,  // "j"
  CTYPE_ALPHA,  // "k"
  CTYPE_ALPHA,  // "l"
  CTYPE_ALPHA,  // "m"
  CTYPE_ALPHA,  // "n"
  CTYPE_ALPHA,  // "o"
  CTYPE_ALPHA,  // "p"
  CTYPE_ALPHA,  // "q"
  CTYPE_ALPHA,  // "r"
  CTYPE_ALPHA,  // "s"
  CTYPE_ALPHA,  // "t"
  CTYPE_ALPHA,  // "u"
  CTYPE_ALPHA,  // "v"
  CTYPE_ALPHA,  // "w"
  CTYPE_ALPHA,  // "x"
  CTYPE_ALPHA,  // "y"
  CTYPE_ALPHA,  // "z"
  CTYPE_NONE,  // "{"
  CTYPE_NONE,  // "|"
  CTYPE_NONE,  // "}"
  CTYPE_MARK,  // "~"
  CTYPE_NONE,  // 0x7f
};

#define HAS_CTYPE(c, mask) ((CTYPES[c & 0x7f] & mask) != 0)
#define HAS_ESCAPED(lex) (lexer_nremaining(lex) >= 3 && lexer_upto(lex)[0] == '%' && HAS_CTYPE(lexer_upto(lex)[1], CTYPE_HEX) && HAS_CTYPE(lexer_upto(lex)[2], CTYPE_HEX))


/**
 * scheme = alpha *( alpha | digit | "+" | "-" | "." )
 **/
static enum status
uri_parse_scheme(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      if (i == 0) {
        return STATUS_BAD;
      }
      else {
        break;
      }
    }

    const char c = lexer_peek(lex);
    if (i == 0) {
      if (!HAS_CTYPE(c, CTYPE_ALPHA)) {
        return STATUS_BAD;
      }
    }
    else if (!HAS_CTYPE(c, CTYPE_SCHEME)) {
      if (i == 0) {
        return STATUS_BAD;
      }
      else {
        break;
      }
    }

    lexer_consume(lex, 1);
  }

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;
  uri->scheme = malloc(nbytes + 1);
  if (uri->scheme == NULL) {
    return STATUS_ENOMEM;
  }
  memcpy((void *)uri->scheme, start, nbytes);
  uri->scheme[nbytes] = '\0';

  return STATUS_OK;
}


/**
 * userinfo = *( unreserved | escaped | ";" | ":" | "&" | "=" | "+" | "$" | "," )
 **/
static enum status
uri_parse_userinfo(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      break;
    }

    const char c = lexer_peek(lex);
    if (HAS_CTYPE(c, CTYPE_USERINFO)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      break;
    }
  }

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;
  if (nbytes != 0) {
    uri->userinfo = malloc(nbytes + 1);
    if (uri->userinfo == NULL) {
      return STATUS_ENOMEM;
    }
    memcpy((void *)uri->userinfo, start, nbytes);
    uri->userinfo[nbytes] = '\0';
  }

  return STATUS_OK;
}


/**
 * hostname     = *( domainlabel "." ) toplabel [ "." ]
 * domainlabel  = alphanum | alphanum *( alphanum | "-" ) alphanum
 * toplabel     = alpha    | alpha    *( alphanum | "-" ) alphanum
 **/
static enum status
uri_parse_hostname(struct lexer *const lex) {
  enum State {
    STATE_1,
    STATE_2,
    STATE_3,
    STATE_2_5,
    STATE_3_4,
    STATE_1_6,
  };

  // Take a copy of the lexer.
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  enum State state = STATE_1;
  for (unsigned int i = 0; ; ++i) {
    // Are there any more bytes to consume?
    if (lexer_nremaining(lex) == 0) {
      switch (state) {
      case STATE_2_5:
      case STATE_1_6:
        break;
      default:
        goto fail;
      }
    }

    const char c = lexer_peek(lex);
    switch (state) {
    case STATE_1:
      if (HAS_CTYPE(c, CTYPE_ALPHA)) {
        lexer_consume(lex, 1);
        state = STATE_2_5;
      }
      else if (HAS_CTYPE(c, CTYPE_DIGIT)) {
        lexer_consume(lex, 1);
        state = STATE_2;
      }
      else {
        goto fail;
      }
      break;

    case STATE_2:
      if (HAS_CTYPE(c, CTYPE_ALNUM)) {
        lexer_consume(lex, 1);
      }
      else if (c == '-') {
        lexer_consume(lex, 1);
        state = STATE_3;
      }
      else {
        goto fail;
      }
      break;

    case STATE_3:
      if (HAS_CTYPE(c, CTYPE_ALNUM)) {
        lexer_consume(lex, 1);
        state = STATE_2;
      }
      else {
        goto fail;
      }
      break;

    case STATE_2_5:
      if (HAS_CTYPE(c, CTYPE_ALNUM)) {
        lexer_consume(lex, 1);
      }
      else if (c == '-') {
        lexer_consume(lex, 1);
        state = STATE_3_4;
      }
      else if (c == '.') {
        lexer_consume(lex, 1);
        state = STATE_1_6;
      }
      else {
        goto fail;
      }
      break;

    case STATE_3_4:
      if (HAS_CTYPE(c, CTYPE_ALNUM)) {
        lexer_consume(lex, 1);
        state = STATE_2_5;
      }
      else {
        goto fail;
      }
      break;

    case STATE_1_6:
      if (HAS_CTYPE(c, CTYPE_ALPHA)) {
        lexer_consume(lex, 1);
        state = STATE_2_5;
      }
      else if (HAS_CTYPE(c, CTYPE_DIGIT)) {
        lexer_consume(lex, 1);
        state = STATE_2;
      }
      else {
        goto fail;
      }
      break;
    }
  }

  return STATUS_OK;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return STATUS_BAD;
}


/**
 * IPv4address = 1*digit "." 1*digit "." 1*digit "." 1*digit
 **/
static enum status
uri_parse_ipv4address(struct lexer *const lex) {
  // Take a copy of the lexer.
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  for (unsigned int i = 0; i != 4; ++i) {
    // "."
    if (i != 0) {
      if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != '.') {
        goto fail;
      }
      lexer_consume(lex, 1);
    }

    // 1*digit
    for (unsigned int j = 0; ; ++j) {
      if (lexer_nremaining(lex) == 0 || !HAS_CTYPE(lexer_peek(lex), CTYPE_DIGIT)) {
        if (i == 0) {
          goto fail;
        }
        else {
          break;
        }
      }
      lexer_consume(lex, 1);
    }
  }

  return STATUS_OK;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return STATUS_BAD;
}


/**
 * hostport = host [ ":" port ]
 * host     = hostname | IPv4address
 * port     = *digit
 **/
static enum status
uri_parse_hostport(struct uri *const uri, struct lexer *const lex) {
  // host
  if (!uri_parse_hostname(lex) && !uri_parse_ipv4address(lex)) {
    return STATUS_BAD;
  }

  // [ ":" port ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == ':') {
    lexer_consume(lex, 1);

    uri->port = 0;
    char c;
    while (lexer_nremaining(lex) != 0 && HAS_CTYPE((c = lexer_peek(lex)), CTYPE_DIGIT)) {
      uri->port = (10 * uri->port) + (c - '0');
      lexer_consume(lex, 1);
    }
  }

  return STATUS_OK;
}


/**
 * server = [ [ userinfo "@" ] hostport ]
 **/
static enum status
uri_parse_server(struct uri *const uri, struct lexer *const lex) {
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  // userinfo
  if (!uri_parse_userinfo(uri, lex)) {
    return STATUS_BAD;
  }

  // [ "@" ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '@') {
    lexer_consume(lex, 1);
  }

  // [ hostport ]
  if (!uri_parse_hostport(uri, lex)) {
    memcpy(lex, &orig, sizeof(struct lexer));
  }

  return STATUS_OK;
}


/**
 * reg_name = 1*( unreserved | escaped | "$" | "," | ";" | ":" | "@" | "&" | "=" | "+" )
 **/
static enum status
uri_parse_reg_name(struct lexer *const lex) {
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      if (i == 0) {
        goto fail;
      }
      else {
        break;
      }
    }

    const char c = lexer_peek(lex);
    if (HAS_CTYPE(c, CTYPE_REG_NAME)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      if (i == 0) {
        goto fail;
      }
      else {
        break;
      }
    }
  }

  return STATUS_OK;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return STATUS_BAD;
}


/**
 * authority = server | reg_name
 **/
enum status
uri_parse_authority(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  enum status status = uri_parse_reg_name(lex);
  if (status == STATUS_ENOMEM) {
    return status;
  }
  else if (status == STATUS_BAD) {
    status = uri_parse_server(uri, lex);
  }

  if (status == STATUS_OK) {
    const char *const end = lexer_upto(lex);
    const size_t nbytes = end - start;
    uri->netloc = malloc(nbytes + 1);
    if (uri->netloc == NULL) {
      return STATUS_OK;
    }
    memcpy((void *)uri->netloc, start, nbytes);
    uri->netloc[nbytes] = '\0';
  }

  return status;
}


/**
 * rel_segment = 1*( unreserved | escaped | ";" | "@" | "&" | "=" | "+" | "$" | "," )
 **/
static enum status
uri_parse_rel_segment(struct lexer *const lex) {
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      if (i == 0) {
        goto fail;
      }
      else {
        break;
      }
    }

    const char c = lexer_peek(lex);
    if (HAS_CTYPE(c, CTYPE_REL_SEGMENT)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      if (i == 0) {
        goto fail;
      }
      else {
        break;
      }
    }
  }

  return STATUS_OK;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return STATUS_BAD;
}


/**
 * path_segments = segment *( "/" segment )
 * segment       = *pchar *( ";" param )
 * param         = *pchar
 * pchar         = unreserved | escaped | ":" | "@" | "&" | "=" | "+" | "$" | ","
 **/
static void
uri_parse_path_segments(struct lexer *const lex) {
  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      break;
    }

    // "/"
    if (i != 0) {
      if (lexer_peek(lex) != '/') {
        break;
      }
      lexer_consume(lex, 1);
    }

    // segment
    while (1) {
      const char c = lexer_peek(lex);
      if (HAS_CTYPE(c, CTYPE_PCHAR)) {
        lexer_consume(lex, 1);
      }
      else if (HAS_ESCAPED(lex)) {
        lexer_consume(lex, 3);
      }
      else {
        break;
      }
    }
    while (1) {
      if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ';') {
        break;
      }
      while (1) {
        const char c = lexer_peek(lex);
        if (HAS_CTYPE(c, CTYPE_PCHAR)) {
          lexer_consume(lex, 1);
        }
        else if (HAS_ESCAPED(lex)) {
          lexer_consume(lex, 3);
        }
        else {
          break;
        }
      }
    }
  }
}


/**
 * abs_path = "/" path_segments
 **/
enum status
uri_parse_abs_path(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return STATUS_BAD;
  }

  const char *const start = lexer_upto(lex);

  // "/"
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != '/') {
    return STATUS_BAD;
  }
  lexer_consume(lex, 1);

  // path_segments
  uri_parse_path_segments(lex);

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;
  uri->path = malloc(nbytes + 1);
  if (uri->path == NULL) {
    return STATUS_ENOMEM;
  }
  memcpy(uri->path, start, nbytes);
  uri->path[nbytes] = '\0';

  return STATUS_OK;
}


/**
 * rel_path = rel_segment [ abs_path ]
 **/
static enum status
uri_parse_rel_path(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  // rel_segment
  if (!uri_parse_rel_segment(lex)) {
    return STATUS_BAD;
  }

  // [ abs_path ]
  uri_parse_abs_path(uri, lex);

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;

  free(uri->path);
  uri->path = malloc(nbytes + 1);
  if (uri->path == NULL) {
    return STATUS_ENOMEM;
  }
  memcpy(uri->path, start, nbytes);
  uri->path[nbytes] = '\0';

  return STATUS_OK;
}


/**
 * net_path  = "//" authority [ abs_path ]
 **/
static enum status
uri_parse_net_path(struct uri *const uri, struct lexer *const lex) {
  // "//"
  if (lexer_nremaining(lex) < 2 || lexer_memcmp(lex, "//", 2) != 0) {
    return STATUS_BAD;
  }
  lexer_consume(lex, 2);

  // authority
  if (!uri_parse_authority(uri, lex)) {
    return STATUS_BAD;
  }

  // [ abs_path ]
  uri_parse_abs_path(uri, lex);

  return STATUS_OK;
}

/**
 * fragment = *uric
 * uric     = reserved | unreserved | escaped
 **/
static enum status
uri_parse_fragment(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  // fragment
  while (1) {
    if (lexer_nremaining(lex) == 0) {
      break;
    }
    else if (HAS_CTYPE(lexer_peek(lex), CTYPE_URIC)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      break;
    }
  }

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;
  if (nbytes != 0) {
    uri->fragment = malloc(nbytes + 1);
    if (uri->fragment == NULL) {
      return STATUS_ENOMEM;
    }
    memcpy(uri->fragment, start, nbytes);
    uri->fragment[nbytes] = '\0';
  }

  return STATUS_OK;
}


/**
 * query = *uric
 * uric  = reserved | unreserved | escaped
 **/
static enum status
uri_parse_query(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  // query
  while (1) {
    if (lexer_nremaining(lex) == 0) {
      break;
    }
    else if (HAS_CTYPE(lexer_peek(lex), CTYPE_URIC)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      break;
    }
  }

  const char *const end = lexer_upto(lex);
  const size_t nbytes = end - start;
  if (nbytes != 0) {
    uri->query = malloc(nbytes + 1);
    if (uri->query == NULL) {
      return STATUS_ENOMEM;
    }
    memcpy(uri->query, start, nbytes);
    uri->query[nbytes] = '\0';
  }

  return STATUS_OK;
}


/**
 * hier_part = ( net_path | abs_path ) [ "?" query ]
 **/
static enum status
uri_parse_hier_part(struct uri *const uri, struct lexer *const lex) {
  // net_path | abs_path | rel_path
  if (!uri_parse_net_path(uri, lex) && !uri_parse_abs_path(uri, lex)) {
    return STATUS_BAD;
  }

  // [ "?" query ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '?') {
    lexer_consume(lex, 1);

    enum status status = uri_parse_query(uri, lex);
    if (status != STATUS_OK) {
      return status;
    }
  }

  return STATUS_OK;
}


/**
 * opaque_part   = uric_no_slash *uric
 * uric_no_slash = unreserved | escaped | ";" | "?" | ":" | "@" | "&" | "=" | "+" | "$" | ","
 * uric          = reserved | unreserved | escaped
 **/
static enum status
uri_parse_opaque_part(struct lexer *const lex) {
  // uric_no_slash
  if (lexer_nremaining(lex) == 0) {
    return STATUS_BAD;
  }
  else if (HAS_CTYPE(lexer_peek(lex), CTYPE_URIC_NO_SLASH)) {
    lexer_consume(lex, 1);
  }
  else if (HAS_ESCAPED(lex)) {
    lexer_consume(lex, 3);
  }
  else {
    return STATUS_BAD;
  }

  // *uric
  while (1) {
    if (lexer_nremaining(lex) == 0) {
      break;
    }
    else if (HAS_CTYPE(lexer_peek(lex), CTYPE_URIC)) {
      lexer_consume(lex, 1);
    }
    else if (HAS_ESCAPED(lex)) {
      lexer_consume(lex, 3);
    }
    else {
      break;
    }
  }

  return STATUS_OK;
}


// ================================================================================================
// Public API for `uri`.
// ================================================================================================
enum status
uri_init(struct uri *const uri) {
  if (uri == NULL) {
    return STATUS_EINVAL;
  }

  memset(uri, 0, sizeof(struct uri));
  return STATUS_OK;
}


enum status
uri_destroy(struct uri *const uri) {
  if (uri == NULL) {
    return STATUS_EINVAL;
  }

  free((void *)uri->scheme);
  free((void *)uri->netloc);
  free((void *)uri->path);
  free((void *)uri->params);
  free((void *)uri->query);
  free((void *)uri->fragment);
  free((void *)uri->userinfo);
  return STATUS_OK;
}


void
uri_pprint(FILE *const file, const struct uri *const uri) {
  if (file == NULL) {
    return;
  }
  if (uri == NULL) {
    fprintf(file, "[(null)]");
  }
  else {
    fprintf(file, "[scheme=%s netloc=%s path=%s params=%s query=%s fragment=%s userinfo=%s port=%u]", uri->scheme, uri->netloc, uri->path, uri->params, uri->query, uri->fragment, uri->userinfo, uri->port);
  }
}


/**
 * URI-reference = [ absoluteURI | relativeURI ] [ "#" fragment ]
 **/
enum status
uri_parse(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return STATUS_EINVAL;
  }

  if (!uri_parse_absolute_uri(uri, lex) && !uri_parse_relative_uri(uri, lex)) {
    return STATUS_BAD;
  }

  // [ "#" fragment ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '#') {
    lexer_consume(lex, 1);

    enum status status = uri_parse_fragment(uri, lex);
    if (status != STATUS_OK) {
      return status;
    }
  }

  return STATUS_OK;
}


/**
 * absoluteURI   = scheme ":" ( hier_part | opaque_part )
 **/
enum status
uri_parse_absolute_uri(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return STATUS_EINVAL;
  }

  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  // scheme
  if (!uri_parse_scheme(uri, lex)) {
    goto fail;
  }

  // ":"
  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ':') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // hier_part | opaque_part
  if (!uri_parse_hier_part(uri, lex) && !uri_parse_opaque_part(lex)) {
    goto fail;
  }

  return STATUS_OK;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return STATUS_BAD;
}


/**
 * relativeURI = ( net_path | abs_path | rel_path ) [ "?" query ]
 **/
enum status
uri_parse_relative_uri(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return STATUS_EINVAL;
  }

  // net_path | abs_path | rel_path
  if (!uri_parse_net_path(uri, lex) && !uri_parse_abs_path(uri, lex) && !uri_parse_rel_path(uri, lex)) {
    return STATUS_BAD;
  }

  // [ "?" query ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '?') {
    lexer_consume(lex, 1);

    enum status status = uri_parse_query(uri, lex);
    if (status != STATUS_OK) {
      return status;
    }
  }

  return STATUS_OK;
}
