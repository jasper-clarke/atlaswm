#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include "atlas.h"

#define MAX_KEYBINDINGS 100
#define MAX_VALUE_LENGTH 256

typedef enum {
  ACTION_SPAWN,
  ACTION_TOGGLEDASH,
  ACTION_RELOAD,
  ACTION_CYCLEFOCUS,
  ACTION_KILLCLIENT,
  ACTION_TOGGLEFLOATING,
  ACTION_FOCUSMONITOR,
  ACTION_MOVETOMONITOR,
  ACTION_QUIT,
  ACTION_UNKNOWN
} ActionType;

typedef struct {
  unsigned int modifier;
  KeySym keysym;
  ActionType action;
  char value[MAX_VALUE_LENGTH];
  char description[MAX_VALUE_LENGTH];
} Keybinding;

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
  int focusMasterOnClose;
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
