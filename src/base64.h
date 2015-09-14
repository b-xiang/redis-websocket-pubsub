/**
 * Base64 is defined in RFC4648
 * https://tools.ietf.org/html/rfc4648
 **/
#pragma once

#include <stdlib.h>

#include "status.h"


struct base64_buffer {
  char *data;
  size_t used;
  size_t allocd;
};


enum status base64_init(struct base64_buffer *buffer);
enum status base64_destroy(struct base64_buffer *buffer);
enum status base64_decode(const char *input, size_t input_nbytes, struct base64_buffer *buffer);
enum status base64_encode(const char *input, size_t input_nbytes, struct base64_buffer *buffer);
