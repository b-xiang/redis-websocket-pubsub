/**
 * The JSON format is defined in RFC7159
 * https://tools.ietf.org/html/rfc7159
 **/
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <event2/buffer.h>

#include "json.h"
#include "lexer.h"
#include "logging.h"


static struct json_value *parse(struct lexer *lex, struct evbuffer *buffer);
static struct json_value *parse_array(struct lexer *lex, struct evbuffer *buffer);
static struct json_value *parse_number(struct lexer *lex);
static struct json_value *parse_object(struct lexer *lex, struct evbuffer *buffer);
static bool               parse_string(struct lexer *lex, struct evbuffer *buffer);


// ================================================================================================
// JSON parsing
// ================================================================================================
static struct json_value *
parse_array(struct lexer *const lex, struct evbuffer *const buffer) {
  struct json_value *value;
  enum status status;

  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != '[') {
    return NULL;
  }

  struct json_value *array = json_value_create(JSON_VALUE_TYPE_ARRAY);
  if (array == NULL) {
    return NULL;
  }

  lexer_consume(lex, 1);

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      json_value_destroy(array);
      return NULL;
    }
    if (lexer_peek(lex) == ']') {
      lexer_consume(lex, 1);
      break;
    }

    if (i != 0) {
      lexer_consume_ws(lex);
      if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ',') {
        json_value_destroy(array);
        return NULL;
      }
      lexer_consume(lex, 1);
    }

    lexer_consume_ws(lex);
    value = parse(lex, buffer);
    if (value == NULL) {
      json_value_destroy(array);
      return NULL;
    }
    status = json_value_append(array, value);
    if (status != STATUS_OK) {
      json_value_destroy(array);
      return NULL;
    }
  }

  return array;
}


static struct json_value *
parse_number(struct lexer *const lex) {
  const char *const start = lexer_upto(lex);
  unsigned int count;
  char c = lexer_peek(lex);

  if (c == '-') {
    lexer_consume(lex, 1);
  }
  while (lexer_nremaining(lex) != 0) {
    c = lexer_peek(lex);
    if (!isdigit(c)) {
      break;
    }
    lexer_consume(lex, 1);
  }
  if (lexer_nremaining(lex) != 0 && lexer_peek(lex) == '.') {
    lexer_consume(lex, 1);
    count = 0;
    while (lexer_nremaining(lex) != 0 && isdigit(lexer_peek(lex))) {
      lexer_consume(lex, 1);
      ++count;
    }
    if (count == 0) {
      return NULL;
    }
  }
  if (lexer_nremaining(lex) != 0 && (lexer_peek(lex) == 'e' || lexer_peek(lex) == 'E')) {
    lexer_consume(lex, 1);
    if (lexer_nremaining(lex) != 0 && (lexer_peek(lex) == '+' || lexer_peek(lex) == '-')) {
      lexer_consume(lex, 1);
    }
    count = 0;
    while (lexer_nremaining(lex) != 0 && isdigit(lexer_peek(lex))) {
      lexer_consume(lex, 1);
      ++count;
    }
    if (count == 0) {
      return NULL;
    }
  }

  struct json_value *value = json_value_create(JSON_VALUE_TYPE_NUMBER);
  if (value == NULL) {
    return NULL;
  }

  const int ret = sscanf(start, "%lf", &value->as.number);
  if (ret != 1) {
    ERROR("parse", "Failed to scanf the double. ret=%d\n", ret);
    json_value_destroy(value);
    return NULL;
  }

  return value;
}


static struct json_value *
parse_object(struct lexer *const lex, struct evbuffer *const buffer) {
  struct json_value *value;
  enum status status;
  bool success;

  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != '{') {
    return NULL;
  }

  struct json_value *const object = json_value_create(JSON_VALUE_TYPE_OBJECT);
  if (object == NULL) {
    return NULL;
  }

  lexer_consume(lex, 1);

  for (unsigned int i = 0; ; ++i) {
    if (lexer_nremaining(lex) == 0) {
      json_value_destroy(object);
      return NULL;
    }
    if (lexer_peek(lex) == '}') {
      lexer_consume(lex, 1);
      break;
    }

    if (i != 0) {
      lexer_consume_ws(lex);
      if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ',') {
        DEBUG("parse_object", "failed to read ',' (%s)\n", lexer_upto(lex));
        json_value_destroy(object);
        return NULL;
      }
      lexer_consume(lex, 1);
    }

    lexer_consume_ws(lex);
    success = parse_string(lex, buffer);
    if (!success) {
      json_value_destroy(object);
      return NULL;
    }
    const size_t length = evbuffer_get_length(buffer);
    char *const string = malloc(length + 1);
    if (string != NULL) {
      evbuffer_copyout(buffer, string, length);
      string[length] = '\0';
    }

    lexer_consume_ws(lex);
    if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != ':') {
      DEBUG0("parse_object", "failed to read ':'\n");
      free(string);
      json_value_destroy(object);
      return NULL;
    }
    lexer_consume(lex, 1);
    lexer_consume_ws(lex);

    value = parse(lex, buffer);
    if (value == NULL) {
      DEBUG0("parse_object", "failed to parse value\n");
      free(string);
      json_value_destroy(object);
      return NULL;
    }

    status = json_value_set_nocopy(object, string, value);
    if (status != STATUS_OK) {
      DEBUG0("parse_object", "failed to json_value_set_nocopy\n");
      free(string);
      json_value_destroy(object);
      return NULL;
    }
  }

  return object;
}


static bool
parse_string(struct lexer *const lex, struct evbuffer *const buffer) {
  char c;
  evbuffer_drain(buffer, evbuffer_get_length(buffer));

  if (lexer_nremaining(lex) == 0 || lexer_peek(lex) != '"') {
    return false;
  }
  lexer_consume(lex, 1);

  while (true) {
    if (lexer_nremaining(lex) == 0) {
      goto fail;
    }
    c = lexer_peek(lex);
    if (c == '"') {
      lexer_consume(lex, 1);
      break;
    }
    else if (c == '\\') {
      lexer_consume(lex, 1);
      if (lexer_nremaining(lex) == 0) {
        goto fail;
      }
      c = lexer_peek(lex);
      switch (c) {
      case '"':
      case '\\':
      case '/':
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      case 'b':
        c = '\b';
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      case 'f':
        c = '\f';
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      case 'n':
        c = '\n';
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      case 'r':
        c = '\r';
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      case 't':
        c = '\t';
        evbuffer_add(buffer, &c, 1);
        lexer_consume(lex, 1);
        break;
      default:
        if (lexer_nremaining(lex) < 5 || c != 'u' || !isxdigit(lexer_upto(lex)[1]) || !isxdigit(lexer_upto(lex)[2]) || !isxdigit(lexer_upto(lex)[3]) || !isxdigit(lexer_upto(lex)[4])) {
          goto fail;
        }
        // TODO convert to Unicode code point and encode in UTF-8.
        lexer_consume(lex, 5);
      }
    }
    else {
      lexer_consume(lex, 1);
      evbuffer_add(buffer, &c, 1);
    }
  }

  return true;

fail:
  evbuffer_drain(buffer, evbuffer_get_length(buffer));
  return false;
}


static struct json_value *
parse(struct lexer *const lex, struct evbuffer *const buffer) {
  struct json_value *value = NULL;

  lexer_consume_ws(lex);
  if (lexer_nremaining(lex) == 0) {
    return NULL;
  }

  if (lexer_peek(lex) == '{') {
    value = parse_object(lex, buffer);
  }
  else if (lexer_peek(lex) == '[') {
    value = parse_array(lex, buffer);
  }
  else if (lexer_peek(lex) == '"') {
    if (parse_string(lex, buffer)) {
      value = json_value_create(JSON_VALUE_TYPE_STRING);
      if (value != NULL) {
        const size_t length = evbuffer_get_length(buffer);
        char *const string = malloc(length + 1);
        if (string != NULL) {
          evbuffer_copyout(buffer, string, length);
          string[length] = '\0';
        }
        value->as.string = string;
      }
    }
  }
  else if (lexer_peek(lex) == '-' || isdigit(lexer_peek(lex))) {
    value = parse_number(lex);
  }
  else if (lexer_nremaining(lex) >= 4 && lexer_memcmp(lex, "true", 4) == 0) {
    lexer_consume(lex, 4);
    value = json_value_create(JSON_VALUE_TYPE_BOOLEAN);
    if (value != NULL) {
      value->as.boolean = true;
    }
  }
  else if (lexer_nremaining(lex) >= 4 && lexer_memcmp(lex, "null", 4) == 0) {
    lexer_consume(lex, 4);
    value = json_value_create(JSON_VALUE_TYPE_NULL);
  }
  else if (lexer_nremaining(lex) >= 5 && lexer_memcmp(lex, "false", 5) == 0) {
    lexer_consume(lex, 5);
    value = json_value_create(JSON_VALUE_TYPE_BOOLEAN);
    if (value != NULL) {
      value->as.boolean = false;
    }
  }
  else {
    return NULL;
  }

  lexer_consume_ws(lex);
  return value;
}


struct json_value *
json_parse(const char *const string) {
  return json_parse_n(string, strlen(string));
}


struct json_value *
json_parse_n(const char *const string, const size_t nbytes) {
  struct lexer lex;
  lexer_init(&lex, string, string + nbytes);

  struct evbuffer *buffer = evbuffer_new();
  if (buffer == NULL) {
    return NULL;
  }

  struct json_value *value = parse(&lex, buffer);
  if (value != NULL && lexer_nremaining(&lex) != 0) {
    json_value_destroy(value);
    value = NULL;
  }

  evbuffer_free(buffer);
  return value;
}


// ================================================================================================
// JSON value CRUD.
// ================================================================================================
enum status
json_value_append(struct json_value *const array, struct json_value *const value) {
  struct json_value_list *pair, *prev, *p;

  if (array == NULL || array->type != JSON_VALUE_TYPE_ARRAY || value == NULL) {
    return STATUS_EINVAL;
  }

  // Allocate memory.
  pair = malloc(sizeof(struct json_value_list));
  if (pair == NULL) {
    return STATUS_ENOMEM;
  }

  // Construct the pair.
  pair->key = NULL;
  pair->value = value;
  pair->next = NULL;

  // Append it to the end of the list of pairs.
  prev = NULL;
  for (p = array->as.pairs; p != NULL; p = p->next) {
    prev = p;
  }
  if (prev == NULL) {
    array->as.pairs = pair;
  }
  else {
    prev->next = pair;
  }

  return STATUS_OK;
}


struct json_value *
json_value_create(const enum json_value_type type) {
  struct json_value *const value = malloc(sizeof(struct json_value));
  if (value == NULL) {
    ERROR0("json_value_create", "malloc failed\n");
    return NULL;
  }

  memset(value, 0, sizeof(struct json_value));
  value->type = type;

  return value;
}


enum status
json_value_destroy(struct json_value *const value) {
  struct json_value_list *pair, *next;

  if (value == NULL) {
    return STATUS_EINVAL;
  }

  switch (value->type) {
  case JSON_VALUE_TYPE_ARRAY:
  case JSON_VALUE_TYPE_OBJECT:
    for (pair = value->as.pairs; pair != NULL; ) {
      next = pair->next;
      free(pair->key);
      json_value_destroy(pair->value);
      free(pair);
      pair = next;
    }
    break;

  case JSON_VALUE_TYPE_BOOLEAN:
  case JSON_VALUE_TYPE_NULL:
  case JSON_VALUE_TYPE_NUMBER:
    break;

  case JSON_VALUE_TYPE_STRING:
    free(value->as.string);
    break;
  }
  free(value);

  return STATUS_OK;
}


struct json_value *
json_value_get(const struct json_value *value, const char *const key) {
  struct json_value_list *pair;

  if (value == NULL || value->type != JSON_VALUE_TYPE_OBJECT) {
    return NULL;
  }

  for (pair = value->as.pairs; pair != NULL; pair = pair->next) {
    if (strcmp(pair->key, key) == 0) {
      return pair->value;
    }
  }

  return NULL;
}


enum status
json_value_set(struct json_value *const object, const char *const key, struct json_value *const value) {
  return json_value_set_n(object, key, strlen(key), value);
}


enum status
json_value_set_n(struct json_value *const object, const char *const key, const size_t key_nbytes, struct json_value *const value) {
  struct json_value_list *pair;
  char *key_copy;

  if (object == NULL || object->type != JSON_VALUE_TYPE_OBJECT || key == NULL || value == NULL) {
    return STATUS_EINVAL;
  }

  // Allocate memory.
  pair = malloc(sizeof(struct json_value_list));
  key_copy = malloc(key_nbytes + 1);
  if (pair == NULL || key_copy == NULL) {
    free(pair);
    free(key_copy);
    return STATUS_ENOMEM;
  }
  memcpy(key_copy, key, key_nbytes);
  key_copy[key_nbytes] = '\0';

  // Construct the pair.
  pair->key = key_copy;
  pair->value = value;
  pair->next = object->as.pairs;
  object->as.pairs = pair;

  return STATUS_OK;
}


enum status
json_value_set_nocopy(struct json_value *const object, char *const key, struct json_value *const value) {
  struct json_value_list *pair;

  if (object == NULL || object->type != JSON_VALUE_TYPE_OBJECT || key == NULL || value == NULL) {
    return STATUS_EINVAL;
  }

  // Allocate memory.
  pair = malloc(sizeof(struct json_value_list));
  if (pair == NULL) {
    return STATUS_ENOMEM;
  }

  // Construct the pair.
  pair->key = key;
  pair->value = value;
  pair->next = object->as.pairs;
  object->as.pairs = pair;

  return STATUS_OK;
}
