#pragma once

#include <stdlib.h>

#include "status.h"


// Forwards declaration.
struct string_pool;


struct string_pool *string_pool_create(void);
enum status         string_pool_destroy(struct string_pool *pool);
const char *        string_pool_get(struct string_pool *pool, const char *str);
enum status         string_pool_release(struct string_pool *pool, const char *str);
