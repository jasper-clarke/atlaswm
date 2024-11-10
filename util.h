#ifndef __UTIL_H__
#define __UTIL_H__

#include <stddef.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))

// Log levels
typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_FATAL
} LogLevel;

// Function declarations
void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
void safe_strcpy(char *dest, const char *src, size_t size);
void set_log_level(LogLevel level);
void log_message(LogLevel level, const char *file, int line, const char *fmt,
                 ...);

#define LOG_DEBUG(fmt, args...)                                                \
  log_message(LOG_DEBUG, __FILE__, __LINE__, fmt, ##args)
#define LOG_INFO(fmt, args...)                                                 \
  log_message(LOG_INFO, __FILE__, __LINE__, fmt, ##args)
#define LOG_WARN(fmt, args...)                                                 \
  log_message(LOG_WARNING, __FILE__, __LINE__, fmt, ##args)
#define LOG_ERROR(fmt, args...)                                                \
  log_message(LOG_ERROR, __FILE__, __LINE__, fmt, ##args)
#define LOG_FATAL(fmt, args...)                                                \
  log_message(LOG_FATAL, __FILE__, __LINE__, fmt, ##args)

#endif // __UTIL_H__
