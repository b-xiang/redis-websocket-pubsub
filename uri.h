/**
 * The URI format is defined in RFC2396
 * https://tools.ietf.org/html/rfc2396
 **/
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "status.h"

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


enum status uri_init(struct uri *uri);
enum status uri_destroy(struct uri *uri);
void uri_pprint(FILE *file, const struct uri *uri);

enum status uri_parse(struct uri *uri, struct lexer *lex);
enum status uri_parse_abs_path(struct uri *uri, struct lexer *lex);
enum status uri_parse_absolute_uri(struct uri *uri, struct lexer *lex);
enum status uri_parse_authority(struct uri *uri, struct lexer *lex);
enum status uri_parse_relative_uri(struct uri *uri, struct lexer *lex);
