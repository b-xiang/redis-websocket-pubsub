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

#define DEBUG(fmt, ...)    logging_log(LOGGING_LEVEL_DEBUG, __FUNCTION__, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define DEBUG0(fmt)        logging_log(LOGGING_LEVEL_DEBUG, __FUNCTION__, __FILE__, __LINE__, fmt)
#define INFO(fmt, ...)     logging_log(LOGGING_LEVEL_INFO, __FUNCTION__, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define INFO0(fmt)         logging_log(LOGGING_LEVEL_INFO, __FUNCTION__, __FILE__, __LINE__, fmt)
#define WARNING(fmt, ...)  logging_log(LOGGING_LEVEL_WARNING, __FUNCTION__, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define WARNING0(fmt)      logging_log(LOGGING_LEVEL_WARNING, __FUNCTION__, __FILE__, __LINE__, fmt)
#define ERROR(fmt, ...)    logging_log(LOGGING_LEVEL_ERROR, __FUNCTION__, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define ERROR0(fmt)        logging_log(LOGGING_LEVEL_ERROR, __FUNCTION__, __FILE__, __LINE__, fmt)
