#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "http.h"
#include "lexer.h"


int
main(void) {
  struct http_request req;
  if (!http_request_init(&req)) {
    perror("http_request_init failed");
    return 1;
  }

  char buffer[4096];
  size_t nbytes;
  {
    const ssize_t ret = read(0, buffer, sizeof(buffer));
    if (ret == -1) {
      perror("read failed");
      return 1;
    }
    nbytes = (size_t)ret;
    buffer[nbytes] = '\0';
  }

  struct lexer lex;
  if (!lexer_init(&lex, buffer, buffer + nbytes)) {
    perror("lexer_init failed");
    return 1;
  }

  const bool success = http_request_parse(&req, &lex);
  printf("http_request_parse => %d\n", success);

  if (!lexer_destroy(&lex)) {
    perror("lexer_destroy failed");
    return 1;
  }
  if (!http_request_destroy(&req)) {
    perror("http_request_destroy failed");
    return 1;
  }

  return 0;
}
