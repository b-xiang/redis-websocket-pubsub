/**
 * Base64 is defined in RFC4648
 * https://tools.ietf.org/html/rfc4648
 **/
#ifndef BASE64_H_
#define BASE64_H_

#include <stdlib.h>

struct base64_buffer {
  char *data;
  size_t used;
  size_t allocd;
};

void base64_init(struct base64_buffer *buffer);
void base64_destroy(struct base64_buffer *buffer);

void base64_encode(const char *input, size_t input_nbytes, struct base64_buffer *buffer);

#endif  // BASE64_H_
