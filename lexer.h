#ifndef LEXER_H_
#define LEXER_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


struct lexer {
  const char *upto;
  const char *end;
  size_t nremaining;
};


bool lexer_init(struct lexer *lex, const char *start, const char *end);
bool lexer_consume_lws(struct lexer *lex);
bool lexer_consume_uint32(struct lexer *lex, uint32_t *number);

#define lexer_consume(lex, nchars) { lex->upto += nchars; lex->nremaining -= nchars; }
#define lexer_nremaining(lex) (lex->end - lex->upto)
#define lexer_peek(lex) (*lex->upto)
#define lexer_strcmp(lex, str) (strcmp(lex->upto, str))
#define lexer_upto(lex) (lex->upto)

#endif
