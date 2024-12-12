#include "config.h" // Includes "atlas.h"
#include "atlas.h"
#include "toml.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global configuration instance
Config cfg = {
    .outerGaps = 20,
    .innerGaps = 10,
    .borderWidth = 3,
    .borderInactiveColor = "#222222",
    .borderActiveColor = "#444444",
    .snapDistance = 0,
    .masterFactor = 0.5,
    .lockFullscreen = 1,
    .focusNewWindows = 1,
    .keybindings = NULL,
    .keybindingCapacity = 0,
    .moveCursorWithFocus = 1,
    .logLevel = "info",
};

static const struct {
  const char *name;
  ActionType action;
} action_map[] = {{"spawn", ACTION_SPAWN},
                  {"reload", ACTION_RELOAD},
                  {"cyclefocus", ACTION_CYCLEFOCUS},
                  {"killclient", ACTION_KILLCLIENT},
                  {"togglefloating", ACTION_TOGGLEFLOATING},
                  {"focusmonitor", ACTION_FOCUSMONITOR},
                  {"movetomonitor", ACTION_MOVETOMONITOR},
                  {"viewworkspace", ACTION_VIEWWORKSPACE},
                  {"movetoworkspace", ACTION_MOVETOWORKSPACE},
                  {"duplicatetoworkspace", ACTION_DUPLICATETOWORKSPACE},
                  {"toggleworkspace", ACTION_TOGGLEWORKSPACE},
                  {"quit", ACTION_QUIT},
                  {NULL, ACTION_UNKNOWN}};

static const struct {
  const char *name;
  unsigned int mask;
} modifier_map[] = {{"Mod1", Mod1Mask},
                    {"Mod4", Mod4Mask},
                    {"Control", ControlMask},
                    {"Shift", ShiftMask},
                    {"Alt", Mod1Mask},
                    {"Super", Mod4Mask},
                    {NULL, 0}};

ActionType string_to_action(const char *action) {
  for (int i = 0; action_map[i].name != NULL; i++) {
    if (strcasecmp(action, action_map[i].name) == 0) {
      return action_map[i].action;
    }
  }
  return ACTION_UNKNOWN;
}

unsigned int parse_modifier(const char *mod) {
  unsigned int mask = 0;
  char *mod_copy = strdup(mod);
  char *token = strtok(mod_copy, "+");

  while (token) {
    for (int i = 0; modifier_map[i].name != NULL; i++) {
      if (strcasecmp(token, modifier_map[i].name) == 0) {
        mask |= modifier_map[i].mask;
        break;
      }
    }
    token = strtok(NULL, "+");
  }

  free(mod_copy);
  return mask;
}

KeySym parse_key(const char *key) {
  // Convert the last token to a keysym
  if (strlen(key) == 1) {
    return XStringToKeysym(key);
  }
  return XStringToKeysym(key);
}

void parse_keybinding(const char *key_str, toml_table_t *binding_table) {
  // Grow keybindings array if needed
  if (cfg.keybindingCount >= cfg.keybindingCapacity) {
    size_t new_capacity =
        cfg.keybindingCapacity == 0 ? 16 : cfg.keybindingCapacity * 2;
    Keybinding *new_keybindings =
        realloc(cfg.keybindings, new_capacity * sizeof(Keybinding));
    if (!new_keybindings) {
      LOG_ERROR("Failed to allocate memory for keybindings");
      return;
    }
    cfg.keybindings = new_keybindings;
    cfg.keybindingCapacity = new_capacity;
  }

  // Parse the key combination
  char *last_plus = strrchr(key_str, '+');
  if (!last_plus) {
    LOG_ERROR("Invalid key binding format: %s", key_str);
    return;
  }

  // Split into modifier and key
  size_t mod_len = last_plus - key_str;
  char *modifier_str = strndup(key_str, mod_len);
  const char *key = last_plus + 1;
  LOG_INFO("Key: %s, Modifier: %s", key, modifier_str);

  // Get the binding properties
  toml_datum_t action = toml_string_in(binding_table, "action");
  toml_datum_t value = toml_string_in(binding_table, "value");
  toml_datum_t desc = toml_string_in(binding_table, "desc");

  if (!action.ok) {
    LOG_ERROR("Keybinding missing action: %s", key_str);
    free(modifier_str);
    return;
  }

  LOG_DEBUG("Action: %s", action.u.s);

  // Create the keybinding
  Keybinding *kb = &cfg.keybindings[cfg.keybindingCount];
  kb->modifier = parse_modifier(modifier_str);
  kb->keysym = parse_key(key);
  kb->action = string_to_action(action.u.s);

  // Allocate and copy value if present
  if (value.ok) {
    kb->value = strdup(value.u.s);
    free(value.u.s);
  } else {
    kb->value = strdup(""); // Empty string instead of NULL
  }

  // Allocate and copy description if present
  if (desc.ok) {
    kb->description = strdup(desc.u.s);
    free(desc.u.s);
  } else {
    kb->description = strdup(""); // Empty string instead of NULL
  }

  free(modifier_str);
  free(action.u.s);
  cfg.keybindingCount++;

  LOG_DEBUG("Added keybinding: %s -> %s", key_str, kb->description);
}

void load_keybindings(toml_table_t *conf) {
  toml_table_t *keybindings = toml_table_in(conf, "keybindings");
  if (!keybindings) {
    LOG_INFO("No keybindings configuration found");
    return;
  }

  cfg.keybindingCount = 0;

  // Get number of entries in the keybindings table
  int keycount = toml_table_nkval(keybindings) + toml_table_ntab(keybindings);

  for (int i = 0; i < keycount; i++) {
    const char *key = toml_key_in(keybindings, i);
    if (!key)
      continue;

    toml_table_t *binding = toml_table_in(keybindings, key);
    if (binding) {
      parse_keybinding(key, binding);
    }
  }
}

// Function to split command string into command and arguments
void parse_startup_program(const char *cmd_str, StartupProgram *prog) {
  char *str = strdup(cmd_str);
  char *token;
  int capacity = 10; // Initial capacity for arguments array

  prog->args = malloc(capacity * sizeof(char *));
  prog->arg_count = 0;

  // Get the command (first word)
  token = strtok(str, " ");
  if (token) {
    prog->command = strdup(token);
    prog->args[prog->arg_count++] = strdup(token);

    // Parse remaining arguments
    while ((token = strtok(NULL, " ")) != NULL) {
      if (prog->arg_count >= capacity - 1) { // -1 for NULL terminator
        capacity *= 2;
        prog->args = realloc(prog->args, capacity * sizeof(char *));
      }
      prog->args[prog->arg_count++] = strdup(token);
    }
  }

  // NULL terminate the arguments array
  prog->args[prog->arg_count] = NULL;
  free(str);
}

// Function to free startup program resources
void free_startup_program(StartupProgram *prog) {
  if (!prog)
    return;

  free(prog->command);

  if (prog->args) {
    for (int i = 0; i < prog->arg_count; i++) {
      free(prog->args[i]);
    }
    free(prog->args);
  }
}

void free_startup_programs(Config *cfg) {
  if (cfg->startup_progs) {
    for (int i = 0; i < cfg->startup_prog_count; i++) {
      free_startup_program(&cfg->startup_progs[i]);
    }
    free(cfg->startup_progs);
    cfg->startup_progs = NULL;
    cfg->startup_prog_count = 0;
  }
}

// Add this to your load_config function after other TOML parsing
void load_startup_programs(toml_table_t *conf) {
  // Free any existing startup programs
  free_startup_programs(&cfg);

  toml_array_t *startup = toml_array_in(conf, "startup_progs");
  if (!startup) {
    LOG_INFO("No startup programs configured");
    return;
  }

  // Count the number of startup programs
  int count = toml_array_nelem(startup);
  if (count <= 0)
    return;

  // Allocate space for startup programs
  cfg.startup_progs = calloc(count, sizeof(StartupProgram));
  cfg.startup_prog_count = count;

  // Parse each startup program
  for (int i = 0; i < count; i++) {
    toml_datum_t prog = toml_string_at(startup, i);
    if (prog.ok) {
      parse_startup_program(prog.u.s, &cfg.startup_progs[i]);
      free(prog.u.s);
    }
  }
}

static void free_workspaces(void) {
  if (cfg.workspaces) {
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      free(cfg.workspaces[i].name);
    }
    free(cfg.workspaces);
    cfg.workspaces = NULL;
    cfg.workspaceCount = 0;
  }
}

static void load_workspaces(toml_table_t *conf) {
  // Free existing workspaces if any
  free_workspaces();

  toml_array_t *workspaces = toml_array_in(conf, "workspaces");
  if (!workspaces) {
    // Set default workspaces if none specified
    cfg.workspaceCount = 9;
    cfg.workspaces = ecalloc(cfg.workspaceCount, sizeof(Workspace));
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      char num[2];
      snprintf(num, sizeof(num), "%zu", i + 1);
      cfg.workspaces[i].name = strdup(num);
    }
    return;
  }

  // Count array elements
  cfg.workspaceCount = 0;
  while (toml_raw_at(workspaces, cfg.workspaceCount)) {
    cfg.workspaceCount++;
  }

  // Allocate workspace array
  cfg.workspaces = ecalloc(cfg.workspaceCount, sizeof(Workspace));

  // Load each workspace
  for (size_t i = 0; i < cfg.workspaceCount; i++) {
    toml_datum_t raw = toml_string_at(workspaces, i);
    if (raw.ok) {
      cfg.workspaces[i].name = strdup(raw.u.s);
      free(raw.u.s);
    } else {
      LOG_ERROR("Failed to parse workspace %zu", i);
      // Use default name as fallback
      char num[2];
      snprintf(num, sizeof(num), "%zu", i + 1);
      cfg.workspaces[i].name = strdup(num);
    }
  }
}

// Add this to update_window_manager_state()
void update_keybindings(void) {
  // Clear existing keybindings
  XUngrabKey(display, AnyKey, AnyModifier, root);

  // Register new keybindings
  updateNumlockMask();
  unsigned int modifiers[] = {0, LockMask, numLockMask, numLockMask | LockMask};

  for (int i = 0; i < cfg.keybindingCount; i++) {
    KeyCode code = XKeysymToKeycode(display, cfg.keybindings[i].keysym);
    if (code) {
      for (size_t j = 0; j < LENGTH(modifiers); j++) {
        XGrabKey(display, code, cfg.keybindings[i].modifier | modifiers[j],
                 root, True, GrabModeAsync, GrabModeAsync);
      }
    }
  }
}

void update_window_manager_state(void) {
  Monitor *m;
  Client *c;

  // Update monitor properties
  for (m = monitors; m; m = m->next) {
    // Update all clients on this monitor
    for (c = m->clients; c; c = c->next) {
      if (!c->isFullscreen) { // Don't modify fullscreen windows
        // Update border width
        c->borderWidth = cfg.borderWidth;
        Clr activeBorderColor;
        drw_clr_create(drawContext, &activeBorderColor, cfg.borderActiveColor);
        Clr inactiveBorderColor;
        drw_clr_create(drawContext, &inactiveBorderColor,
                       cfg.borderInactiveColor);
        XSetWindowBorder(display, c->win,
                         (c == selectedMonitor->active)
                             ? activeBorderColor.pixel
                             : inactiveBorderColor.pixel);
        // Apply border width change
        XWindowChanges wc = {.x = c->x,
                             .y = c->y,
                             .width = c->w,
                             .height = c->h,
                             .border_width = c->borderWidth};
        XConfigureWindow(display, c->win,
                         CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
      }
    }

    // Update master factor and number of master windows
    m->masterFactor = cfg.masterFactor;
  }

  setNumDesktops();
  setCurrentDesktop();
  setDesktopNames();
  setViewport();

  // Rearrange all monitors to apply gap changes and new layouts
  arrange(NULL);

  // Update keybindings
  update_keybindings();

  // Sync changes
  XSync(display, False);
}

int load_config(const char *config_path) {
  FILE *fp;
  char errbuf[200];

  // Open config file
  fp = fopen(config_path, "r");
  if (!fp) {
    LOG_ERROR("Failed to open config file: %s", config_path);
    return 0;
  }

  // Parse TOML
  toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
  fclose(fp);

  if (!conf) {
    LOG_ERROR("Failed to parse config file: %s", errbuf);
    return 0;
  }

  // Gaps
  toml_table_t *gaps = toml_table_in(conf, "gaps");
  if (gaps) {
    toml_datum_t outer = toml_int_in(gaps, "outer");
    if (outer.ok) {
      cfg.outerGaps = outer.u.i;
    }

    toml_datum_t inner = toml_int_in(gaps, "inner");
    if (inner.ok) {
      cfg.innerGaps = inner.u.i;
    }
  }

  // Read border configuration
  toml_table_t *border = toml_table_in(conf, "border");
  if (border) {
    toml_datum_t width = toml_int_in(border, "width");
    if (width.ok) {
      cfg.borderWidth = width.u.i;
    }

    toml_datum_t active = toml_string_in(border, "active");
    if (active.ok) {
      safe_strcpy(cfg.borderActiveColor, active.u.s,
                  sizeof(cfg.borderActiveColor));
      free(active.u.s);
    }

    toml_datum_t inactive = toml_string_in(border, "inactive");
    if (inactive.ok) {
      safe_strcpy(cfg.borderInactiveColor, inactive.u.s,
                  sizeof(cfg.borderInactiveColor));
      free(inactive.u.s);
    }
  }

  // Layout configuration
  toml_table_t *layout = toml_table_in(conf, "layout");
  if (layout) {
    toml_datum_t master_factor = toml_double_in(layout, "master_factor");
    if (master_factor.ok) {
      cfg.masterFactor = master_factor.u.d;
    }
  }

  // window configuration
  toml_table_t *windows = toml_table_in(conf, "windows");
  if (windows) {
    toml_datum_t focus_new_windows = toml_bool_in(windows, "focus_new_windows");
    if (focus_new_windows.ok) {
      cfg.focusNewWindows = focus_new_windows.u.b;
    }

    toml_datum_t move_cursor_with_focus =
        toml_bool_in(windows, "move_cursor_with_focus");
    if (move_cursor_with_focus.ok) {
      cfg.moveCursorWithFocus = move_cursor_with_focus.u.b;
    }
  }

  toml_datum_t log_level = toml_string_in(conf, "log_level");
  if (log_level.ok) {
    // If log_level matches any avaliable log level, use it
    if (strcmp(log_level.u.s, "debug") == 0 ||
        strcmp(log_level.u.s, "info") == 0 ||
        strcmp(log_level.u.s, "warning") == 0) {
      cfg.logLevel = strdup(log_level.u.s);
      free(log_level.u.s);
    } else {
      LOG_WARN("Invalid log level: %s", log_level.u.s);
    }
  }

  load_keybindings(conf);
  load_startup_programs(conf);
  load_workspaces(conf);

  toml_free(conf);
  return 1;
}

void reload_config(void) {
  char config_path[256];
  char *home = getenv("HOME");
  if (!home) {
    LOG_ERROR("Could not get HOME directory");
    return;
  }

  snprintf(config_path, sizeof(config_path), "%s/.config/atlaswm/config.toml",
           home);

  if (load_config(config_path)) {
    LOG_INFO("Configuration reloaded successfully");
    update_window_manager_state();
  } else {
    LOG_ERROR("Failed to reload configuration");
  }
}
