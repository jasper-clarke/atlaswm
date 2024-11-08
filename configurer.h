#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include "atlas.h"

// Configuration structure
typedef struct {
  int outerGaps;
  int innerGaps;
} Config;

// Global configuration instance
extern Config cfg;

// Function declarations
int load_config(const char *config_path);
void apply_config(void);
void reload_config(void);

#endif // _CONFIG_MANAGER_H_
