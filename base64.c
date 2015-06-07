/**
 * Base64 is defined in RFC4648
 * https://tools.ietf.org/html/rfc4648
 **/
#include "base64.h"

#include <stdint.h>
#include <string.h>

static const char TABLE[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


void
base64_init(struct base64_buffer *const buffer) {
  if (buffer == NULL) {
    return;
  }

  memset(buffer, 0, sizeof(struct base64_buffer));
}


void
base64_destroy(struct base64_buffer *const buffer) {
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
}


void
base64_encode(const char *const input_start, const size_t input_nbytes, struct base64_buffer *const buffer) {
  // Work out how many output bytes we need to store the base64'd input data.
  size_t output_nbytes = input_nbytes / 3;
  if (input_nbytes % 3 != 0) {
    ++output_nbytes;
  }
  output_nbytes *= 4;

  // Grow the working buffer if needed.
  if (buffer->allocd < output_nbytes) {
    free(buffer->data);
    buffer->data = malloc(output_nbytes);
    if (buffer->data == NULL) {
      buffer->allocd = 0;
      buffer->used = 0;
      return;
    }

    buffer->allocd = output_nbytes;
  }
  buffer->used = output_nbytes;

  char *output = buffer->data;
  const char *const input_end = input_start + input_nbytes;
  for (const char *input = input_start; input < input_end; input += 3) {
    uint32_t bytes = (input[0] << 16);
    if (input + 1 >= input_end) {
      *output++ = TABLE[(bytes >> 18) & 0x3f];
      *output++ = TABLE[(bytes >> 12) & 0x3f];
      *output++ = '=';
      *output++ = '=';
    }
    else if (input + 2 >= input_end) {
      bytes |= (input[1] << 8);
      *output++ = TABLE[(bytes >> 18) & 0x3f];
      *output++ = TABLE[(bytes >> 12) & 0x3f];
      *output++ = TABLE[(bytes >>  6) & 0x3f];
      *output++ = '=';
    }
    else {
      bytes |= (input[1] << 8) | input[2];
      *output++ = TABLE[(bytes >> 18) & 0x3f];
      *output++ = TABLE[(bytes >> 12) & 0x3f];
      *output++ = TABLE[(bytes >>  6) & 0x3f];
      *output++ = TABLE[(bytes >>  0) & 0x3f];
    }
  }
}
