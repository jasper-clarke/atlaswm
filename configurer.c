#include "configurer.h" // Includes "atlas.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml.h>

// Global configuration instance
Config cfg = {
    .outerGaps = 20,
    .innerGaps = 10,
    .borderWidth = 3,
    .snapDistance = 0,
    .masterFactor = 0.5,
    .numMasterWindows = 1,
    .lockFullscreen = 1,
    .showDash = 1,
    .topBar = 1,
    .focusNewWindows = 1,
    .moveCursorWithFocus = 1,
};

static const struct {
  const char *name;
  ActionType action;
} action_map[] = {{"spawn", ACTION_SPAWN},
                  {"toggledash", ACTION_TOGGLEDASH},
                  {"reload", ACTION_RELOAD},
                  {"cyclefocus", ACTION_CYCLEFOCUS},
                  {"killclient", ACTION_KILLCLIENT},
                  {"togglefloating", ACTION_TOGGLEFLOATING},
                  {"focusmonitor", ACTION_FOCUSMONITOR},
                  {"movetomonitor", ACTION_MOVETOMONITOR},
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

void parse_keybinding(const char *key_str, toml_table_t *binding_table,
                      Config *cfg) {
  if (cfg->keybindingCount >= MAX_KEYBINDINGS) {
    LOG_ERROR("Maximum number of keybindings reached");
    return;
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

  LOG_INFO("Action: %s", action.u.s);

  // Create the keybinding
  Keybinding *kb = &cfg->keybindings[cfg->keybindingCount];
  kb->modifier = parse_modifier(modifier_str);
  kb->keysym = parse_key(key);
  kb->action = string_to_action(action.u.s);

  if (value.ok) {
    strncpy(kb->value, value.u.s, MAX_VALUE_LENGTH - 1);
    free(value.u.s);
  } else {
    kb->value[0] = '\0';
  }

  if (desc.ok) {
    strncpy(kb->description, desc.u.s, MAX_VALUE_LENGTH - 1);
    free(desc.u.s);
  } else {
    kb->description[0] = '\0';
  }

  free(modifier_str);
  free(action.u.s);
  cfg->keybindingCount++;

  LOG_INFO("Added keybinding: %s -> %s", key_str, kb->description);
}

void load_keybindings(toml_table_t *conf, Config *cfg) {
  toml_table_t *keybindings = toml_table_in(conf, "keybindings");
  if (!keybindings) {
    LOG_INFO("No keybindings configuration found");
    return;
  }

  cfg->keybindingCount = 0;

  // Get number of entries in the keybindings table
  int keycount = toml_table_nkval(keybindings) + toml_table_ntab(keybindings);

  for (int i = 0; i < keycount; i++) {
    const char *key = toml_key_in(keybindings, i);
    if (!key)
      continue;

    toml_table_t *binding = toml_table_in(keybindings, key);
    if (binding) {
      parse_keybinding(key, binding, cfg);
    }
  }
}

// Add this to update_window_manager_state()
void update_keybindings(void) {
  // Clear existing keybindings
  XUngrabKey(dpy, AnyKey, AnyModifier, root);

  // Register new keybindings
  updatenumlockmask();
  unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};

  for (int i = 0; i < cfg.keybindingCount; i++) {
    KeyCode code = XKeysymToKeycode(dpy, cfg.keybindings[i].keysym);
    if (code) {
      for (size_t j = 0; j < LENGTH(modifiers); j++) {
        XGrabKey(dpy, code, cfg.keybindings[i].modifier | modifiers[j], root,
                 True, GrabModeAsync, GrabModeAsync);
      }
    }
  }
}

void update_window_manager_state(void) {
  Monitor *m;
  Client *c;

  // Update monitor properties
  for (m = monitors; m; m = m->next) {
    // Update bar visibility and position
    m->showDash = cfg.showDash;
    m->dashPos = cfg.topBar;
    updateDashPosition(m);
    XMoveResizeWindow(dpy, m->dashWin, m->wx, m->dashPos, m->ww, bh);

    // Update all clients on this monitor
    for (c = m->clients; c; c = c->next) {
      if (!c->isFullscreen) { // Don't modify fullscreen windows
        // Update border width
        c->borderWidth = cfg.borderWidth;
        XSetWindowBorder(dpy, c->win,
                         (c == selectedMonitor->active)
                             ? scheme[SchemeSel][ColBorder].pixel
                             : scheme[SchemeNorm][ColBorder].pixel);

        // Apply border width change
        XWindowChanges wc = {.x = c->x,
                             .y = c->y,
                             .width = c->w,
                             .height = c->h,
                             .border_width = c->borderWidth};
        XConfigureWindow(dpy, c->win,
                         CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
      }
    }

    // Update master factor and number of master windows
    m->masterFactor = cfg.masterFactor;
    m->numMasterWindows = cfg.numMasterWindows;
  }

  // Force a redraw of bars
  drawDashes();

  // Rearrange all monitors to apply gap changes and new layouts
  arrange(NULL);

  // Update keybindings
  update_keybindings();

  // Sync changes
  XSync(dpy, False);
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

  // Dash configuration
  toml_table_t *dash = toml_table_in(conf, "dashboard");
  if (dash) {
    toml_datum_t show = toml_bool_in(dash, "show");
    if (show.ok) {
      cfg.showDash = show.u.b;
    }

    toml_datum_t top = toml_string_in(dash, "position");
    if (top.ok) {
      cfg.topBar = (strcmp(top.u.s, "top") == 0);
      free(top.u.s);
    }
  }

  // Layout configuration
  toml_table_t *layout = toml_table_in(conf, "layout");
  if (layout) {
    toml_datum_t master_factor = toml_double_in(layout, "master_factor");
    if (master_factor.ok) {
      cfg.masterFactor = master_factor.u.d;
    }

    toml_datum_t master_count = toml_int_in(layout, "master_count");
    if (master_count.ok) {
      cfg.numMasterWindows = master_count.u.i;
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

  // Load keybindings
  load_keybindings(conf, &cfg);

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
