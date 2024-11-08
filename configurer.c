#include "configurer.h" // Includes "atlas.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml.h>

// Global configuration instance
Config cfg = {.outerGaps = 20, // Default values matching original config.h
              .innerGaps = 10};

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

  // Read gaps configuration
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

  toml_free(conf);
  return 1;
}

void apply_config(void) {
  // This will be called after loading new config to apply changes
  // For now, just arranging all monitors will apply the new gap settings
  arrange(NULL);
}

void reload_config(void) {
  // For now, hardcode the config path to ~/.config/atlaswm/config.toml
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
    apply_config();
  } else {
    LOG_ERROR("Failed to reload configuration");
  }
}
