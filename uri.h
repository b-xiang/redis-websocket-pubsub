/**
 * The URI format is defined in RFC2396
 * https://tools.ietf.org/html/rfc2396
 **/
#ifndef URI_H_
#define URI_H_

#include <stdint.h>
#include <stdio.h>

// Forwards declaration from lexer.h.
struct lexer;


enum uri_parse_status {
  URI_PARSE_BAD = 0,
  URI_PARSE_OK,
  URI_PARSE_ENOMEM,
};


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


enum uri_parse_status uri_init(struct uri *uri);
enum uri_parse_status uri_destroy(struct uri *uri);
void uri_pprint(FILE *file, const struct uri *uri);

enum uri_parse_status uri_parse(struct uri *uri, struct lexer *lex);
enum uri_parse_status uri_parse_abs_path(struct uri *uri, struct lexer *lex);
enum uri_parse_status uri_parse_absolute_uri(struct uri *uri, struct lexer *lex);
enum uri_parse_status uri_parse_authority(struct uri *uri, struct lexer *lex);
enum uri_parse_status uri_parse_relative_uri(struct uri *uri, struct lexer *lex);

#endif  // URI_H_
