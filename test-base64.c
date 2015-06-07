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
  {"", ""},
};


int
main(void) {
  struct base64_buffer buffer;

  base64_init(&buffer);
  static const size_t ntests = sizeof(TESTS)/sizeof(struct test_case);
  for (size_t i = 0; i != ntests; ++i) {
    fprintf(stdout, "Test %zu/%zu) ", i + 1, ntests);

    base64_encode(TESTS[i].input, strlen(TESTS[i].input), &buffer);
    if (buffer.data == NULL) {
      fprintf(stdout, "error! buffer->data is NULL");
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

    fprintf(stdout, "\n");
  }
  base64_destroy(&buffer);

  return 0;
}
