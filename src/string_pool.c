#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "string_pool.h"
#include "xxhash.h"

#define HASHTABLE_NBUCKETS (2063)  // Arbitrary "large enough" prime.


struct node {
  char *str;
  size_t refcount;
  struct node *next;
};


struct string_pool {
  struct node *table[HASHTABLE_NBUCKETS];
};


struct string_pool *
string_pool_create(void) {
  struct string_pool *const pool = malloc(sizeof(struct string_pool));
  if (pool == NULL) {
    ERROR0("malloc failed.\n");
    return NULL;
  }
  memset(pool, 0, sizeof(struct string_pool));
  return pool;
}


enum status
string_pool_destroy(struct string_pool *const pool) {
  struct node *node, *next;

  if (pool == NULL) {
    return STATUS_EINVAL;
  }

  for (size_t i = 0; i != HASHTABLE_NBUCKETS; ++i) {
    for (node = pool->table[i]; node != NULL; ) {
      next = node->next;
      free(node->str);
      free(node);
      node = next;
    }
  }
  free(pool);

  return STATUS_OK;
}


const char *
string_pool_get(struct string_pool *const pool, const char *const lookup) {
  struct node *node, *prev;
  char *str;

  if (pool == NULL || lookup == NULL) {
    return NULL;
  }

  const size_t lookup_length = strlen(lookup);
  const size_t bucket = XXH64(lookup, lookup_length, 0) % HASHTABLE_NBUCKETS;
  prev = NULL;
  for (node = pool->table[bucket]; node != NULL; node = node->next) {
    if (strcmp(node->str, lookup) == 0) {
      break;
    }
    prev = node;
  }

  // Does the string not already exist in the pool?
  if (node == NULL) {
    node = malloc(sizeof(struct node));
    str = malloc(lookup_length + 1);
    if (node == NULL || str == NULL) {
      ERROR0("malloc failed.\n");
      free(node);
      free(str);
      return NULL;
    }
    memset(node, 0, sizeof(struct node));
    node->str = strcpy(str, lookup);

    if (prev == NULL) {
      pool->table[bucket] = node;
    }
    else {
      prev->next = node;
    }
  }

  // Increment the refcount and return.
  ++node->refcount;
  return node->str;
}


enum status
string_pool_release(struct string_pool *const pool, const char *const str) {
  struct node *node, *prev;

  if (pool == NULL || str == NULL) {
    return STATUS_EINVAL;
  }

  const size_t bucket = XXH64(str, strlen(str), 0) % HASHTABLE_NBUCKETS;
  prev = NULL;
  for (node = pool->table[bucket]; node != NULL; node = node->next) {
    if (node->str == str) {
      --node->refcount;
      if (node->refcount == 0) {
        if (prev == NULL) {
          pool->table[bucket] = node->next;
        }
        else {
          prev->next = node->next;
        }
        free(node->str);
        free(node);
      }
      return STATUS_OK;
    }
    prev = node;
  }

  return STATUS_BAD;
}
