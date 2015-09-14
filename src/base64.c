/**
 * Base64 is defined in RFC4648
 * https://tools.ietf.org/html/rfc4648
 **/
#include "base64.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const uint8_t DECODE_TABLE[128] = {
  UINT8_MAX,  // 0x00
  UINT8_MAX,  // 0x01
  UINT8_MAX,  // 0x02
  UINT8_MAX,  // 0x03
  UINT8_MAX,  // 0x04
  UINT8_MAX,  // 0x05
  UINT8_MAX,  // 0x06
  UINT8_MAX,  // 0x07
  UINT8_MAX,  // 0x08
  UINT8_MAX,  // 0x09
  UINT8_MAX,  // 0x0a
  UINT8_MAX,  // 0x0b
  UINT8_MAX,  // 0x0c
  UINT8_MAX,  // 0x0d
  UINT8_MAX,  // 0x0e
  UINT8_MAX,  // 0x0f
  UINT8_MAX,  // 0x10
  UINT8_MAX,  // 0x11
  UINT8_MAX,  // 0x12
  UINT8_MAX,  // 0x13
  UINT8_MAX,  // 0x14
  UINT8_MAX,  // 0x15
  UINT8_MAX,  // 0x16
  UINT8_MAX,  // 0x17
  UINT8_MAX,  // 0x18
  UINT8_MAX,  // 0x19
  UINT8_MAX,  // 0x1a
  UINT8_MAX,  // 0x1b
  UINT8_MAX,  // 0x1c
  UINT8_MAX,  // 0x1d
  UINT8_MAX,  // 0x1e
  UINT8_MAX,  // 0x1f
  UINT8_MAX,  // " "
  UINT8_MAX,  // "!"
  UINT8_MAX,  // """
  UINT8_MAX,  // "#"
  UINT8_MAX,  // "$"
  UINT8_MAX,  // "%"
  UINT8_MAX,  // "&"
  UINT8_MAX,  // "'"
  UINT8_MAX,  // "("
  UINT8_MAX,  // ")"
  UINT8_MAX,  // "*"
  62,  // "+"
  UINT8_MAX,  // ","
  UINT8_MAX,  // "-"
  UINT8_MAX,  // "."
  63,  // "/"
  52,  // "0"
  53,  // "1"
  54,  // "2"
  55,  // "3"
  56,  // "4"
  57,  // "5"
  58,  // "6"
  59,  // "7"
  60,  // "8"
  61,  // "9"
  UINT8_MAX,  // ":"
  UINT8_MAX,  // ";"
  UINT8_MAX,  // "<"
  UINT8_MAX,  // "="
  UINT8_MAX,  // ">"
  UINT8_MAX,  // "?"
  UINT8_MAX,  // "@"
  0,  // "A"
  1,  // "B"
  2,  // "C"
  3,  // "D"
  4,  // "E"
  5,  // "F"
  6,  // "G"
  7,  // "H"
  8,  // "I"
  9,  // "J"
  10,  // "K"
  11,  // "L"
  12,  // "M"
  13,  // "N"
  14,  // "O"
  15,  // "P"
  16,  // "Q"
  17,  // "R"
  18,  // "S"
  19,  // "T"
  20,  // "U"
  21,  // "V"
  22,  // "W"
  23,  // "X"
  24,  // "Y"
  25,  // "Z"
  UINT8_MAX,  // "["
  UINT8_MAX,  // "\"
  UINT8_MAX,  // "]"
  UINT8_MAX,  // "^"
  UINT8_MAX,  // "_"
  UINT8_MAX,  // "`"
  26,  // "a"
  27,  // "b"
  28,  // "c"
  29,  // "d"
  30,  // "e"
  31,  // "f"
  32,  // "g"
  33,  // "h"
  34,  // "i"
  35,  // "j"
  36,  // "k"
  37,  // "l"
  38,  // "m"
  39,  // "n"
  40,  // "o"
  41,  // "p"
  42,  // "q"
  43,  // "r"
  44,  // "s"
  45,  // "t"
  46,  // "u"
  47,  // "v"
  48,  // "w"
  49,  // "x"
  50,  // "y"
  51,  // "z"
  UINT8_MAX,  // "{"
  UINT8_MAX,  // "|"
  UINT8_MAX,  // "}"
  UINT8_MAX,  // "~"
  UINT8_MAX,  // 0x7f
};

static const char ENCODE_TABLE[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


enum status
base64_init(struct base64_buffer *const buffer) {
  if (buffer == NULL) {
    return STATUS_EINVAL;
  }

  memset(buffer, 0, sizeof(struct base64_buffer));
  return STATUS_OK;
}


enum status
base64_destroy(struct base64_buffer *const buffer) {
  if (buffer == NULL) {
    return STATUS_EINVAL;
  }

  free(buffer->data);
  return STATUS_OK;
}


enum status
base64_decode(const char *const input_start, const size_t input_nbytes, struct base64_buffer *const buffer) {
  if (input_start == NULL || buffer == NULL) {
    return STATUS_EINVAL;
  }

  // There must be a multiple of 4 input bytes.
  if (input_nbytes % 4 != 0) {
    return STATUS_BAD;
  }

  // Grow the working buffer if needed.
  const size_t output_nbytes = 3 * (input_nbytes / 4);
  if (buffer->allocd < output_nbytes) {
    free(buffer->data);
    buffer->data = malloc(output_nbytes);
    if (buffer->data == NULL) {
      buffer->allocd = 0;
      buffer->used = 0;
      return STATUS_ENOMEM;
    }

    buffer->allocd = output_nbytes;
  }
  buffer->used = output_nbytes;

  const uint8_t *input = (const uint8_t *)input_start;
  uint8_t *output = (uint8_t *)buffer->data;
  bool padding[2] = {false, false};

  for (size_t i = 0; i != input_nbytes; ) {
    // Read in the 24 bits from the next 4 bytes.
    uint32_t bytes = 0;
    for (size_t j = 0; j != 4; ++j, ++i) {
      if (input[i] >= 128) {
        return STATUS_BAD;
      }
      const uint8_t mask = DECODE_TABLE[input[i] & 0x7f];
      if (input[i] == '=') {
        if (i == input_nbytes - 2) {
          padding[1] = true;
        }
        else if (i == input_nbytes - 1) {
          padding[0] = true;
        }
        else {
          return STATUS_BAD;
        }
        bytes <<= 6;
      }
      else if (mask == UINT8_MAX) {
        return STATUS_BAD;
      }
      else {
        bytes <<= 6;
        bytes |= mask;
      }
    }

    if (padding[1]) {
      // Ensure that if the 2nd last byte was a padding byte, the last byte was also a padding byte.
      if (!padding[0]) {
        return STATUS_BAD;
      }
      *output++ = (bytes >> 16) & 0xff;
      buffer->used -= 2;
    }
    else if (padding[0]) {
      *output++ = (bytes >> 16) & 0xff;
      *output++ = (bytes >>  8) & 0xff;
      buffer->used -= 1;
    }
    else {
      *output++ = (bytes >> 16) & 0xff;
      *output++ = (bytes >>  8) & 0xff;
      *output++ = (bytes >>  0) & 0xff;
    }
  }

  return STATUS_OK;
}


enum status
base64_encode(const char *const input_start, const size_t input_nbytes, struct base64_buffer *const buffer) {
  if (input_start == NULL || buffer == NULL) {
    return STATUS_EINVAL;
  }

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
      return STATUS_ENOMEM;
    }

    buffer->allocd = output_nbytes;
  }
  buffer->used = output_nbytes;

  char *output = buffer->data;
  const uint8_t *const input_end = (const uint8_t *)(input_start + input_nbytes);
  for (const uint8_t *input = (const uint8_t *)input_start; input < input_end; input += 3) {
    uint32_t bytes = (input[0] << 16);
    if (input + 1 >= input_end) {
      *output++ = ENCODE_TABLE[(bytes >> 18) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >> 12) & 0x3f];
      *output++ = '=';
      *output++ = '=';
    }
    else if (input + 2 >= input_end) {
      bytes |= (input[1] << 8);
      *output++ = ENCODE_TABLE[(bytes >> 18) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >> 12) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >>  6) & 0x3f];
      *output++ = '=';
    }
    else {
      bytes |= (input[1] << 8) | input[2];
      *output++ = ENCODE_TABLE[(bytes >> 18) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >> 12) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >>  6) & 0x3f];
      *output++ = ENCODE_TABLE[(bytes >>  0) & 0x3f];
    }
  }

  return STATUS_OK;
}
