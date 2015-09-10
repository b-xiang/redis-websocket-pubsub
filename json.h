/**
 * The JSON format is defined in RFC7159
 * https://tools.ietf.org/html/rfc7159
 **/
#ifndef JSON_H_
#define JSON_H_

#include <stdbool.h>
#include <stdint.h>

#include "status.h"


struct json_value;

enum json_value_type {
  JSON_VALUE_TYPE_ARRAY,
  JSON_VALUE_TYPE_BOOLEAN,
  JSON_VALUE_TYPE_NULL,
  JSON_VALUE_TYPE_NUMBER,
  JSON_VALUE_TYPE_OBJECT,
  JSON_VALUE_TYPE_STRING,
};


struct json_value_list {
  char *key;
  struct json_value *value;
  struct json_value_list *next;
};


struct json_value {
  enum json_value_type type;
  union {
    bool boolean;
    double number;
    struct json_value_list *pairs;
    char *string;
  } as;
};


struct json_value *json_parse(const char *string);
struct json_value *json_parse_n(const char *string, size_t nbytes);

enum status        json_value_append(struct json_value *array, struct json_value *value);
struct json_value *json_value_create(enum json_value_type type);
enum status        json_value_destroy(struct json_value *value);
struct json_value *json_value_get(const struct json_value *value, const char *key);
enum status        json_value_set(struct json_value *object, const char *key, struct json_value *value);
enum status        json_value_set_n(struct json_value *object, const char *key, size_t key_nbytes, struct json_value *value);
enum status        json_value_set_nocopy(struct json_value *object, char *key, struct json_value *value);


#endif  // JSON_H_
