#include <stdio.h>
#include <string.h>

#include "base64.h"


struct test_case {
  const char *input;
  const char *output;
};

static const struct test_case TESTS[] = {
  {"Man", "TWFu"},
  {"any carnal pleas", "YW55IGNhcm5hbCBwbGVhcw=="},
  {"any carnal pleasu", "YW55IGNhcm5hbCBwbGVhc3U="},
  {"any carnal pleasur", "YW55IGNhcm5hbCBwbGVhc3Vy"},
  {"any carnal pleasure", "YW55IGNhcm5hbCBwbGVhc3VyZQ=="},
  {"any carnal pleasure.", "YW55IGNhcm5hbCBwbGVhc3VyZS4="},
  {"pleasure.", "cGxlYXN1cmUu"},
  {"leasure.", "bGVhc3VyZS4="},
  {"easure.", "ZWFzdXJlLg=="},
  {"asure.", "YXN1cmUu"},
  {"sure.", "c3VyZS4="},
  // Test cases from https://tools.ietf.org/html/rfc4648#section-10
  {"", ""},
  {"f", "Zg=="},
  {"fo", "Zm8="},
  {"foo", "Zm9v"},
  {"foob", "Zm9vYg=="},
  {"fooba", "Zm9vYmE="},
  {"foobar", "Zm9vYmFy"},
};


int
main(void) {
  struct base64_buffer buffer;
  enum status status;

  status = base64_init(&buffer);
  if (status != STATUS_OK) {
    perror("base64_init failed");
    return 1;
  }

  static const size_t ntests = sizeof(TESTS)/sizeof(struct test_case);
  for (size_t i = 0; i != ntests; ++i) {
    fprintf(stdout, "Test %zu/%zu) encode: ", i + 1, ntests);

    status = base64_encode(TESTS[i].input, strlen(TESTS[i].input), &buffer);
    if (status != STATUS_OK) {
      fprintf(stdout, "error! base64_encode failed");
    }
    else if (buffer.used != strlen(TESTS[i].output)) {
      fprintf(stdout, "failed! actual length %zu != expected length %zu\n", buffer.used, strlen(TESTS[i].output));
    }
    else if (memcmp(buffer.data, TESTS[i].output, buffer.used) != 0) {
      fprintf(stdout, "failed! actual '");
      fwrite(buffer.data, 1, buffer.used, stdout);
      fprintf(stdout, "' != expected '");
      fwrite(TESTS[i].output, 1, strlen(TESTS[i].output), stdout);
      fprintf(stdout, "'");
    }
    else {
      fprintf(stdout, "passed!");
    }

    fprintf(stdout, " decode: ");

    status = base64_decode(TESTS[i].output, strlen(TESTS[i].output), &buffer);
    if (status != STATUS_OK) {
      fprintf(stdout, "error! base64_decode failed");
    }
    else if (buffer.used != strlen(TESTS[i].input)) {
      fprintf(stdout, "failed! actual length %zu != expected length %zu\n", buffer.used, strlen(TESTS[i].input));
    }
    else if (memcmp(buffer.data, TESTS[i].input, buffer.used) != 0) {
      fprintf(stdout, "failed! actual '");
      fwrite(buffer.data, 1, buffer.used, stdout);
      fprintf(stdout, "' != expected '");
      fwrite(TESTS[i].input, 1, strlen(TESTS[i].input), stdout);
      fprintf(stdout, "'");
    }
    else {
      fprintf(stdout, "passed!");
    }

    fprintf(stdout, "\n");
  }

  status = base64_destroy(&buffer);
  if (status != STATUS_OK) {
    perror("base64_destroy failed");
    return 1;
  }

  return 0;
}
