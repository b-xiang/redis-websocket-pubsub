/**
 * The URI format is defined in RFC2396
 * https://tools.ietf.org/html/rfc2396
 **/
#include "uri.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

static const uint16_t CTYPE_NONE                = 0;
static const uint16_t CTYPE_ALPHA               = 1 <<  0;
static const uint16_t CTYPE_DIGIT               = 1 <<  1;
static const uint16_t CTYPE_HEX                 = 1 <<  2;
static const uint16_t CTYPE_MARK                = 1 <<  3;
static const uint16_t CTYPE_RESERVED            = 1 <<  4;
static const uint16_t CTYPE_PCHAR_EXTRA         = 1 <<  5;
static const uint16_t CTYPE_USERINFO_EXTRA      = 1 <<  6;
static const uint16_t CTYPE_REG_NAME_EXTRA      = 1 <<  7;
static const uint16_t CTYPE_SCHEME_EXTRA        = 1 <<  8;
static const uint16_t CTYPE_REL_SEGMENT_EXTRA   = 1 <<  9;
static const uint16_t CTYPE_URIC_NO_SLASH_EXTRA = 1 << 10;
static const uint16_t CTYPE_ALNUM         = CTYPE_ALPHA | CTYPE_DIGIT;
static const uint16_t CTYPE_UNRESERVED    = CTYPE_ALNUM | CTYPE_MARK;
static const uint16_t CTYPE_URIC          = CTYPE_RESERVED | CTYPE_UNRESERVED;
static const uint16_t CTYPE_PCHAR         = CTYPE_UNRESERVED | CTYPE_PCHAR_EXTRA;
static const uint16_t CTYPE_USERINFO      = CTYPE_UNRESERVED | CTYPE_USERINFO_EXTRA;
static const uint16_t CTYPE_REG_NAME      = CTYPE_UNRESERVED | CTYPE_REG_NAME_EXTRA;
static const uint16_t CTYPE_SCHEME        = CTYPE_ALNUM | CTYPE_SCHEME_EXTRA;
static const uint16_t CTYPE_REL_SEGMENT   = CTYPE_UNRESERVED | CTYPE_REL_SEGMENT_EXTRA;
static const uint16_t CTYPE_URIC_NO_SLASH = CTYPE_UNRESERVED | CTYPE_URIC_NO_SLASH_EXTRA;

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
static bool
uri_parse_scheme(struct uri *const uri, struct lexer *const lex) {
  const char *const start = lexer_upto(lex);

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      if (i == 0) {
        return false;
      }
      else {
        break;
      }
    }

    const char c = lexer_peek(lex);
    if (i == 0) {
      if (!HAS_CTYPE(c, CTYPE_ALPHA)) {
        return false;
      }
    }
    else if (!HAS_CTYPE(c, CTYPE_SCHEME)) {
      if (i == 0) {
        return false;
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
    return false;
  }
  memcpy((void *)uri->scheme, start, nbytes);
  uri->scheme[nbytes] = '\0';

  return true;
}


/**
 * userinfo = *( unreserved | escaped | ";" | ":" | "&" | "=" | "+" | "$" | "," )
 **/
static bool
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
      return false;
    }
    memcpy((void *)uri->userinfo, start, nbytes);
    uri->userinfo[nbytes] = '\0';
  }

  return true;
}


/**
 * hostname     = *( domainlabel "." ) toplabel [ "." ]
 * domainlabel  = alphanum | alphanum *( alphanum | "-" ) alphanum
 * toplabel     = alpha    | alpha    *( alphanum | "-" ) alphanum
 **/
static bool
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

  return true;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return false;
}


/**
 * IPv4address = 1*digit "." 1*digit "." 1*digit "." 1*digit
 **/
static bool
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

  return true;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return false;
}


/**
 * hostport = host [ ":" port ]
 * host     = hostname | IPv4address
 * port     = *digit
 **/
static bool
uri_parse_hostport(struct uri *const uri, struct lexer *const lex) {
  // host
  if (!uri_parse_hostname(lex) || !uri_parse_ipv4address(lex)) {
    return false;
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

  return true;
}


/**
 * server = [ [ userinfo "@" ] hostport ]
 **/
static bool
uri_parse_server(struct uri *const uri, struct lexer *const lex) {
  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  // userinfo
  if (!uri_parse_userinfo(uri, lex)) {
    return false;
  }

  // [ "@" ]
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '@') {
    lexer_consume(lex, 1);
  }

  // [ hostport ]
  if (!uri_parse_hostport(uri, lex)) {
    memcpy(lex, &orig, sizeof(struct lexer));
  }

  return true;
}


/**
 * reg_name = 1*( unreserved | escaped | "$" | "," | ";" | ":" | "@" | "&" | "=" | "+" )
 **/
static bool
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

  return true;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return false;
}


/**
 * authority = server | reg_name
 **/
bool
uri_parse_authority(struct uri *const uri, struct lexer *const lex) {
  return uri_parse_reg_name(lex) || uri_parse_server(uri, lex);
}


/**
 * rel_segment = 1*( unreserved | escaped | ";" | "@" | "&" | "=" | "+" | "$" | "," )
 **/
static bool
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

  return true;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return false;
}


// ================================================================================================
// Public API for `uri`.
// ================================================================================================
bool
uri_init(struct uri *const uri) {
  if (uri == NULL) {
    return false;
  }

  memset(uri, 0, sizeof(struct uri));
  return true;
}


bool
uri_destroy(struct uri *const uri) {
  if (uri == NULL) {
    return false;
  }

  free((void *)uri->scheme);
  free((void *)uri->netloc);
  free((void *)uri->path);
  free((void *)uri->params);
  free((void *)uri->query);
  free((void *)uri->fragment);
  free((void *)uri->userinfo);
  return true;
}

/**
 * URI-reference = [ absoluteURI | relativeURI ] [ "#" fragment ]
 **/
bool
uri_parse(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return false;
  }

  if (!uri_parse_absolute_uri(uri, lex) && !uri_parse_relative_uri(uri, lex)) {
    return false;
  }
  // TODO parse [ "#" fragment ]

  return true;
}


/**
 * absoluteURI   = scheme ":" ( hier_part | opaque_part )
 **/
bool
uri_parse_absolute_uri(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return false;
  }

  struct lexer orig;
  memcpy(&orig, lex, sizeof(struct lexer));

  // scheme
  if (!uri_parse_scheme(uri, lex)) {
    goto fail;
  }

  // ":"
  if (lexer_nremaining(lex) < 1 || lexer_peek(lex) != ':') {
    goto fail;
  }
  lexer_consume(lex, 1);

  // TODO ( hier_part | opaque_part )

  return true;

fail:
  memcpy(lex, &orig, sizeof(struct lexer));
  return false;
}


bool
uri_parse_relative_uri(struct uri *const uri, struct lexer *const lex) {
  if (uri == NULL || lex == NULL) {
    return false;
  }

  // TODO

  return true;
}
