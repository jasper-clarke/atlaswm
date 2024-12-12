#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include "atlas.h"

// Configuration structure
typedef struct {
  // Gaps
  unsigned int outerGaps;
  unsigned int innerGaps;
  // Border
  unsigned int borderWidth;
  char borderActiveColor[8];
  char borderInactiveColor[8];

  // Layout
  int snapDistance;
  float masterFactor;
  int lockFullscreen;

  // Window
  int focusNewWindows;
  int moveCursorWithFocus;

  // Keybindings
  Keybinding *keybindings;
  size_t keybindingCount;
  size_t keybindingCapacity;

  // Workspaces
  Workspace *workspaces;
  size_t workspaceCount;

  // General
  StartupProgram *startup_progs;
  int startup_prog_count;
  char *logLevel;
} Config;

// Global configuration instance
extern Config cfg;

extern Display *display;
extern Monitor *monitors;
extern Monitor *selectedMonitor;

// Function declarations
int load_config(const char *config_path);
void apply_config(void);
void reload_config(void);
ActionType string_to_action(const char *action);
unsigned int parse_modifier(const char *mod);
KeySym parse_key(const char *key);
void register_keybinding(Keybinding *binding);
void free_startup_programs(Config *cfg);
void parse_startup_program(const char *cmd_str, StartupProgram *prog);

#endif // _CONFIG_MANAGER_H_
