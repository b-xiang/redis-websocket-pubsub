#pragma once

#include <stdio.h>

#define DEBUG(function, format, ...)    fprintf(stderr, "\033[1;34m[DEBUG][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG0(function, format)        fprintf(stderr, "\033[1;34m[DEBUG][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__)
#define INFO(function, format, ...)     fprintf(stderr, "\033[1;32m[INFO][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__, __VA_ARGS__)
#define INFO0(function, format)         fprintf(stderr, "\033[1;32m[INFO][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__)
#define WARNING(function, format, ...)  fprintf(stderr, "\033[1;33m[WARNING][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__, __VA_ARGS__)
#define WARNING0(function, format)      fprintf(stderr, "\033[1;33m[WARNING][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__)
#define ERROR(function, format, ...)    fprintf(stderr, "\033[1;31m[ERROR][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR0(function, format)        fprintf(stderr, "\033[1;31m[ERROR][" function ":%s:%d]\033[0m " format, __FILE__, __LINE__)
