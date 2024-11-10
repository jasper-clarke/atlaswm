#include "atlas.h"
#include "util.h"
#include <X11/Xatom.h>

static Atom net_atoms[NetLast];

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
