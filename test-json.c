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
    ERROR0("test_number", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR0("test_number", "value is not type NUMBER\n");
    goto fail;
  }
  if (value->as.number != 345) {
    ERROR("test_number", "%lf != 345\n", value->as.number);
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
    ERROR0("test_null", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_NULL) {
    ERROR0("test_null", "value is not type NULL\n");
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
    ERROR0("test_true", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_BOOLEAN) {
    ERROR0("test_true", "value is not type BOOLEAN\n");
    goto fail;
  }
  if (value->as.boolean != true) {
    ERROR0("test_true", "value is not true\n");
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
    ERROR0("test_false", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_BOOLEAN) {
    ERROR0("test_false", "value is not type BOOLEAN\n");
    goto fail;
  }
  if (value->as.boolean != false) {
    ERROR0("test_false", "value is not false\n");
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
    ERROR0("test_string", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_STRING) {
    ERROR0("test_string", "value is not type STRING\n");
    goto fail;
  }
  if (strcmp(value->as.string, "woot") != 0) {
    ERROR("test_string", "'%s' != 'woot'\n", value->as.string);
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
    ERROR0("test_empty_object", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("test_empty_object", "value is not type OBJECT\n");
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
    ERROR0("test_simple_object_string_value", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("test_simple_object_string_value", "value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("test_simple_object_string_value", "object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("test_simple_object_string_value", "element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_STRING) {
    ERROR("test_simple_object_string_value", "element 0 is not type STRING (%d)\n", pair->value->type);
    goto fail;
  }
  if (strcmp("1", pair->value->as.string) != 0) {
    ERROR("test_simple_object_string_value", "element 0 value '%s' != '1'\n", pair->value->as.string);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("test_simple_object_string_value", "element 1 should not exist but it does\n");
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
    ERROR0("test_space_tester", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("test_space_tester", "value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("test_space_tester", "object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("test_space_tester", "element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_STRING) {
    ERROR("test_space_tester", "element 0 is not type STRING (%d)\n", pair->value->type);
    goto fail;
  }
  if (strcmp("1", pair->value->as.string) != 0) {
    ERROR("test_space_tester", "element 0 value '%s' != '1'\n", pair->value->as.string);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("test_space_tester", "element 1 should not exist but it does\n");
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
    ERROR0("test_simple_object_int_value", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_OBJECT) {
    ERROR0("test_simple_object_int_value", "value is not type OBJECT\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("test_simple_object_int_value", "object is empty\n");
    goto fail;
  }
  if (strcmp("v", pair->key) != 0) {
    ERROR("test_simple_object_int_value", "element 0 key '%s' != 'v'\n", pair->key);
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("test_simple_object_int_value", "element 0 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 1) {
    ERROR("test_simple_object_int_value", "element 0 value '%lf' != 1\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("test_simple_object_int_value", "element 1 should not exist but it does\n");
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
    ERROR0("test_simple_digit_array", "value is NULL\n");
    goto fail;
  }
  if (value->type != JSON_VALUE_TYPE_ARRAY) {
    ERROR0("test_simple_digit_array", "value is not type ARRAY\n");
    goto fail;
  }
  struct json_value_list *pair = value->as.pairs;
  if (pair == NULL) {
    ERROR0("test_simple_digit_array", "object is empty\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("test_simple_digit_array", "element 0 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 1) {
    ERROR("test_simple_digit_array", "element 0 value '%lf' != 1\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair == NULL) {
    ERROR0("test_simple_digit_array", "element 1 not exist but it doesn't\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("test_simple_digit_array", "element 1 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 2) {
    ERROR("test_simple_digit_array", "element 1 value '%lf' != 2\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair == NULL) {
    ERROR0("test_simple_digit_array", "element 2 not exist but it doesn't\n");
    goto fail;
  }
  if (pair->value->type != JSON_VALUE_TYPE_NUMBER) {
    ERROR("test_simple_digit_array", "element 2 is not type NUMBER (%d)\n", pair->value->type);
    goto fail;
  }
  if (pair->value->as.number != 3) {
    ERROR("test_simple_digit_array", "element 2 value '%lf' != 3\n", pair->value->as.number);
    goto fail;
  }
  pair = pair->next;
  if (pair != NULL) {
    ERROR0("test_simple_digit_array", "element 3 exists when it shouldn't\n");
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
