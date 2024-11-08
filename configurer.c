#include "configurer.h" // Includes "atlas.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml.h>

// Global configuration instance
Config cfg = {.outerGaps = 20,
              .innerGaps = 10,
              .borderWidth = 3,
              .snapDistance = 0,
              .masterFactor = 0.5,
              .numMasterWindows = 1,
              .lockFullscreen = 1,
              .showDash = 1,
              .topBar = 1};

void update_window_manager_state(void) {
  Monitor *m;
  Client *c;

  // Update monitor properties
  for (m = monitors; m; m = m->next) {
    // Update bar visibility and position
    m->showbar = cfg.showDash;
    m->topbar = cfg.topBar;
    updateDashPosition(m);
    XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);

    // Update all clients on this monitor
    for (c = m->clients; c; c = c->next) {
      if (!c->isFullscreen) { // Don't modify fullscreen windows
        // Update border width
        c->borderWidth = cfg.borderWidth;
        XSetWindowBorder(dpy, c->win,
                         (c == selectedMonitor->sel)
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
      strncpy(cfg.borderActiveColor, active.u.s, sizeof(cfg.borderActiveColor));
      free(active.u.s);
    }

    toml_datum_t inactive = toml_string_in(border, "inactive");
    if (inactive.ok) {
      strncpy(cfg.borderInactiveColor, inactive.u.s,
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
