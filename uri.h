/**
 * The URI format is defined in RFC2396
 * https://tools.ietf.org/html/rfc2396
 **/
#ifndef URI_H_
#define URI_H_

#include <stdbool.h>
#include <stdint.h>


// Forwards declaration from lexer.h.
struct lexer;

struct uri {
  char *scheme;
  char *netloc;
  char *path;
  char *params;
  char *query;
  char *fragment;
  char *userinfo;
  uint32_t port;
};


bool uri_init(struct uri *uri);
bool uri_destroy(struct uri *uri);
bool uri_parse(struct uri *uri, struct lexer *lex);
bool uri_parse_abs_path(struct uri *uri, struct lexer *lex);
bool uri_parse_absolute_uri(struct uri *uri, struct lexer *lex);
bool uri_parse_authority(struct uri *uri, struct lexer *lex);
bool uri_parse_relative_uri(struct uri *uri, struct lexer *lex);

#endif  // URI_H_
