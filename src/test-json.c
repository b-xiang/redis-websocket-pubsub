#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "logging.h"


static bool
test_number(void) {
  struct json_value *const value = json_parse("345");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR0("value is not type NUMBER\n");
    goto fail;
  }
  if (value->as.number != 345) {
    ERROR("%lf != 345\n", value->as.number);
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_null(void) {
  struct json_value *const value = json_parse("null");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_NULL) {
    ERROR0("value is not type NULL\n");
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_true(void) {
  struct json_value *const value = json_parse("true");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_BOOLEAN) {
    ERROR0("value is not type BOOLEAN\n");
    goto fail;
  }
  if (value->as.boolean != true) {
    ERROR0("value is not true\n");
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_false(void) {
  struct json_value *const value = json_parse("false");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_BOOLEAN) {
    ERROR0("value is not type BOOLEAN\n");
    goto fail;
  }
  if (value->as.boolean != false) {
    ERROR0("value is not false\n");
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_string(void) {
  struct json_value *const value = json_parse("\"woot\"");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_STRING) {
    ERROR0("value is not type STRING\n");
    goto fail;
  }
  if (strcmp(value->as.string, "woot") != 0) {
    ERROR("'%s' != 'woot'\n", value->as.string);
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_empty_object(void) {
  struct json_value *const value = json_parse("{}");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("value is not type OBJECT\n");
    goto fail;
  }
  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_simple_object_string_value(void) {
  struct json_value *const value = json_parse("{ \"v\":\"1\"}");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_STRING) {
    ERROR("element 0 is not type STRING (%d)\n", pair->value->type);
    goto fail;
  }
  if (strcmp("1", pair->value->as.string) != 0) {
    ERROR("element 0 value '%s' != '1'\n", pair->value->as.string);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("element 1 should not exist but it does\n");
    goto fail;
  }

  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_space_tester(void) {
  struct json_value *const value = json_parse("{ \"v\":\"1\"\r\n}");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_STRING) {
    ERROR("element 0 is not type STRING (%d)\n", pair->value->type);
    goto fail;
  }
  if (strcmp("1", pair->value->as.string) != 0) {
    ERROR("element 0 value '%s' != '1'\n", pair->value->as.string);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("element 1 should not exist but it does\n");
    goto fail;
  }

  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_simple_object_int_value(void) {
  struct json_value *const value = json_parse("{ \"v\":1}");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("element 0 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 1) {
    ERROR("element 0 value '%lf' != 1\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("element 1 should not exist but it does\n");
    goto fail;
  }

  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


static bool
test_simple_digit_array(void) {
  struct json_value *const value = json_parse("[1,2,3]");
  if (value == NULL) {
    ERROR0("value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_ARRAY) {
    ERROR0("value is not type ARRAY\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("object is empty\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("element 0 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 1) {
    ERROR("element 0 value '%lf' != 1\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair == NULL) {
    ERROR0("element 1 not exist but it doesn't\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("element 1 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 2) {
    ERROR("element 1 value '%lf' != 2\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair == NULL) {
    ERROR0("element 2 not exist but it doesn't\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("element 2 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 3) {
    ERROR("element 2 value '%lf' != 3\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("element 3 exists when it shouldn't\n");
    goto fail;
  }

  json_value_destroy(value);
  return true;

fail:
  json_value_destroy(value);
  return false;
}


typedef bool(*test_function_t)(void);
static const test_function_t TEST_CASES[] = {
  &test_number,
  &test_null,
  &test_true,
  &test_false,
  &test_string,
  &test_empty_object,
  &test_simple_object_string_value,
  &test_space_tester,
  &test_simple_object_int_value,
  &test_simple_digit_array,
  NULL,
};


int
main(void) {
  unsigned int npassed = 0, nfailed = 0;
  for (size_t i = 0; ; ++i) {
    if (TEST_CASES[i] == NULL) {
      break;
    }
    else if (TEST_CASES[i]()) {
      ++npassed;
    }
    else {
      ++nfailed;
    }
  }
  printf("#passed: %d\n", npassed);
  printf("#failed: %d\n", nfailed);
  return 0;
}
