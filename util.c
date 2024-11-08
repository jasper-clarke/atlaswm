#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

// Global log file pointer
static FILE *log_file = NULL;

// Function to get the home directory
static char *get_home_directory() {
  char *home = getenv("HOME");
  return home ? home : NULL;
}

// Function to initialize the log file
static void init_log_file() {
  if (log_file != NULL)
    return; // Already initialized

  char *home = get_home_directory();
  if (home == NULL) {
    fprintf(stderr, "Could not determine home directory for log file\n");
    return;
  }

  char log_path[512];
  snprintf(log_path, sizeof(log_path), "%s/.atlaslogs", home);

  log_file = fopen(log_path, "a");
  if (log_file == NULL) {
    fprintf(stderr, "Could not open log file %s: %s\n", log_path,
            strerror(errno));
    return;
  }

  // Set the buffer to line buffered mode
  setvbuf(log_file, NULL, _IOLBF, 0);
}

void set_log_level(LogLevel level) {
  if (level >= LOG_DEBUG && level <= LOG_FATAL) {
    current_log_level = level;
  }
}

void log_message(LogLevel level, const char *file, int line, const char *fmt,
                 ...) {
  if (level < current_log_level)
    return;

  // Initialize log file if not already done
  if (log_file == NULL) {
    init_log_file();
  }

  // Get current time
  time_t now;
  time(&now);
  char timestamp[26];
  ctime_r(&now, timestamp);
  timestamp[24] = '\0'; // Remove newline

  // Format the base message
  char base_msg[1024];
  if (level < LOG_WARNING) {
    snprintf(base_msg, sizeof(base_msg), "[%s] %s: ", timestamp,
             log_level_strings[level]);
  } else {
    snprintf(base_msg, sizeof(base_msg), "[%s] %s (%s:%d): ", timestamp,
             log_level_strings[level], file, line);
  }

  // Write to stderr with colors
  fprintf(stderr, "%s%s%s", log_level_colors[level], log_level_strings[level],
          ANSI_COLOR_RESET);
  if (level >= LOG_WARNING) {
    fprintf(stderr, " (%s:%d):", file, line);
  } else {
    fprintf(stderr, " ");
  }

  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");

  // Write to log file if available
  if (log_file != NULL) {
    va_list args2;
    va_start(args2, fmt);
    fprintf(log_file, "%s", base_msg);
    vfprintf(log_file, fmt, args2);
    fprintf(log_file, "\n");
    va_end(args2);
    fflush(log_file);
  }

  // Handle fatal errors
  if (level == LOG_FATAL) {
    if (log_file != NULL) {
      fclose(log_file);
    }
    die("AtlasWM: Shutting down due to fatal error");
  }
}

void die(const char *fmt, ...) {
  va_list ap;
  int saved_errno = errno;

  // Log the fatal error
  if (log_file != NULL) {
    va_list ap_copy;
    va_copy(ap_copy, ap);
    fprintf(log_file, "[FATAL] ");
    vfprintf(log_file, fmt, ap_copy);
    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
      fprintf(log_file, " %s", strerror(saved_errno));
    }
    fprintf(log_file, "\n");
    fclose(log_file);
    va_end(ap_copy);
  }

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    fprintf(stderr, " %s", strerror(saved_errno));
  }
  fputc('\n', stderr);

  exit(1);
}

void *ecalloc(size_t nmemb, size_t size) {
  void *p;

  if (!(p = calloc(nmemb, size)))
    die("calloc:");
  return p;
}
