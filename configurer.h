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
  // Dash
  int showDash;
  int topBar;
  // char *fonts[];

  // Layout
  int snapDistance;
  float masterFactor;
  int numMasterWindows;
  int lockFullscreen;

  // Window
  int focusNewWindows;
  int moveCursorWithFocus;

  // Keybindings
  Keybinding keybindings[MAX_KEYBINDINGS];
  int keybindingCount;
} Config;

// Global configuration instance
extern Config cfg;

extern Display *dpy;
extern Monitor *monitors;
extern Monitor *selectedMonitor;
extern Clr **scheme;
extern int bh; // bar height

// Function declarations
int load_config(const char *config_path);
void apply_config(void);
void reload_config(void);
// Add these function declarations
ActionType string_to_action(const char *action);
unsigned int parse_modifier(const char *mod);
KeySym parse_key(const char *key);
void register_keybinding(Keybinding *binding);

#endif // _CONFIG_MANAGER_H_
