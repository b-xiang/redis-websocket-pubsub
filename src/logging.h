#pragma once


enum logging_level {
  LOGGING_LEVEL_DEBUG,
  LOGGING_LEVEL_INFO,
  LOGGING_LEVEL_WARNING,
  LOGGING_LEVEL_ERROR,
};


void logging_open(const char *path);
void logging_close(void);
void logging_log(enum logging_level level, const char *function_name, const char *file_name, int line_number, const char *fmt, ...);

#define DEBUG(function, fmt, ...)    logging_log(LOGGING_LEVEL_DEBUG, function, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define DEBUG0(function, fmt)        logging_log(LOGGING_LEVEL_DEBUG, function, __FILE__, __LINE__, fmt)
#define INFO(function, fmt, ...)     logging_log(LOGGING_LEVEL_INFO, function, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define INFO0(function, fmt)         logging_log(LOGGING_LEVEL_DEBUG, function, __FILE__, __LINE__, fmt)
#define WARNING(function, fmt, ...)  logging_log(LOGGING_LEVEL_WARNING, function, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define WARNING0(function, fmt)      logging_log(LOGGING_LEVEL_DEBUG, function, __FILE__, __LINE__, fmt)
#define ERROR(function, fmt, ...)    logging_log(LOGGING_LEVEL_ERROR, function, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define ERROR0(function, fmt)        logging_log(LOGGING_LEVEL_DEBUG, function, __FILE__, __LINE__, fmt)
