#include "atlas.h"
#include "util.h"
#include <X11/Xatom.h>

/* HACK: Need to implement TOML config for these */
static const char *tags[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
/* HACK: End of hack*/

static Atom net_atoms[NetLast];

// Regular WM atom names
static const char *wm_atom_names[] = {"WM_PROTOCOLS", "WM_DELETE_WINDOW",
                                      "WM_STATE", "WM_TAKE_FOCUS"};

// EWMH atom names
static const char *net_atom_names[] = {"_NET_SUPPORTED",
                                       "_NET_WM_NAME",
                                       "_NET_WM_STATE",
                                       "_NET_WM_CHECK",
                                       "_NET_SUPPORTING_WM_CHECK",
                                       "_NET_WM_STATE_FULLSCREEN",
                                       "_NET_ACTIVE_WINDOW",
                                       "_NET_WM_WINDOW_TYPE",
                                       "_NET_WM_WINDOW_TYPE_DIALOG",
                                       "_NET_CLIENT_LIST",
                                       "_NET_NUMBER_OF_DESKTOPS",
                                       "_NET_DESKTOP_GEOMETRY",
                                       "_NET_DESKTOP_VIEWPORT",
                                       "_NET_CURRENT_DESKTOP",
                                       "_NET_DESKTOP_NAMES",
                                       "_NET_WORKAREA",
                                       "_NET_SHOWING_DESKTOP"};

void ewmh_update_client_list(void) {
  // Count total clients
  unsigned int num_clients = 0;
  Monitor *m;
  Client *c;

  for (m = monitors; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      num_clients++;

  // Allocate array for window IDs
  Window *windows = malloc(sizeof(Window) * num_clients);
  if (!windows) {
    LOG_ERROR("Failed to allocate memory for client list");
    return;
  }

  // Fill window array
  unsigned int i = 0;
  for (m = monitors; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      windows[i++] = c->win;

  // Set property
  XChangeProperty(dpy, root, net_atoms[NetClientList], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)windows, num_clients);

  free(windows);
}

void ewmh_update_active_window(void) {
  Window active = selectedMonitor->active ? selectedMonitor->active->win : None;
  XChangeProperty(dpy, root, net_atoms[NetActiveWindow], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&active, 1);
}
