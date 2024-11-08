#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

// ANSI color codes for different log levels
#define ANSI_COLOR_RED "\033[48;5;124;1m"
#define ANSI_COLOR_GREEN "\033[48;5;22;1m"
#define ANSI_COLOR_ORANGE "\033[48;5;202;1m"
#define ANSI_COLOR_BLUE "\033[48;5;19;1m"
#define ANSI_COLOR_MAGENTA "\033[48;5;55;1m"
#define ANSI_COLOR_CYAN "\033[48;5;25;1m"
#define ANSI_COLOR_RESET "\033[0m"

// Log level strings
static const char *log_level_strings[] = {"DEBUG", "INFO", "WARNING", "ERROR",
                                          "FATAL"};

// Log level colors
static const char *log_level_colors[] = {
    ANSI_COLOR_BLUE,   // Debug
    ANSI_COLOR_CYAN,   // Info
    ANSI_COLOR_ORANGE, // Warning
    ANSI_COLOR_RED,    // Error
    ANSI_COLOR_MAGENTA // Fatal
};

// Global log level setting (can be changed at runtime)
static LogLevel current_log_level = LOG_INFO;

void set_log_level(LogLevel level) {
  if (level >= LOG_DEBUG && level <= LOG_FATAL) {
    current_log_level = level;
  }
}

void log_message(LogLevel level, const char *file, int line, const char *fmt,
                 ...) {
  if (level < current_log_level)
    return;

  // If log level is greater than warning print file and line number
  if (level < LOG_WARNING) {
    fprintf(stderr, "%s %s %s ", log_level_colors[level],
            log_level_strings[level], ANSI_COLOR_RESET);
  } else {
    fprintf(stderr, "%s %s %s (%s:%d):%s ", log_level_colors[level],
            log_level_strings[level], ANSI_COLOR_RESET, file, line,
            ANSI_COLOR_RESET);
  }

  // Print the actual message
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");

  // Flush immediately for fatal errors
  if (level == LOG_FATAL) {
    fflush(stderr);
    die("AtlasWM: Shutting down due to fatal error");
  }
}

void die(const char *fmt, ...) {
  va_list ap;
  int saved_errno;

  saved_errno = errno;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
    fprintf(stderr, " %s", strerror(saved_errno));
  fputc('\n', stderr);

  exit(1);
}

void *ecalloc(size_t nmemb, size_t size) {
  void *p;

  if (!(p = calloc(nmemb, size)))
    die("calloc:");
  return p;
}
