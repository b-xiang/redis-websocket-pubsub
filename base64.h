/**
 * Base64 is defined in RFC4648
 * https://tools.ietf.org/html/rfc4648
 **/
#ifndef BASE64_H_
#define BASE64_H_

#include <stdlib.h>


enum base64_status {
  BASE64_STATUS_BAD,
  BASE64_STATUS_OK,
  BASE64_STATUS_ENOMEM,
};


struct base64_buffer {
  char *data;
  size_t used;
  size_t allocd;
};


enum base64_status base64_init(struct base64_buffer *buffer);
enum base64_status base64_destroy(struct base64_buffer *buffer);
enum base64_status base64_decode(const char *input, size_t input_nbytes, struct base64_buffer *buffer);
enum base64_status base64_encode(const char *input, size_t input_nbytes, struct base64_buffer *buffer);

#endif  // BASE64_H_
