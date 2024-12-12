#include "atlas.h"
#include "config.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

void attach(Client *c) {
  if (!c->monitor->clients) {
    // If there are no clients, this becomes the first one
    c->monitor->clients = c;
    c->next = NULL;
    return;
  }

  // Find the last client
  Client *last;
  for (last = c->monitor->clients; last->next; last = last->next)
    ;

  // Append the new client
  last->next = c;
  c->next = NULL;
}

void detach(Client *c) {
  Client **tc;

  for (tc = &c->monitor->clients; *tc && *tc != c; tc = &(*tc)->next)
    ;
  *tc = c->next;
}

void attachWindowToStack(Client *c) {
  c->nextInStack = c->monitor->stack;
  c->monitor->stack = c;
}

void detachWindowFromStack(Client *c) {
  Client **tc, *t;

  for (tc = &c->monitor->stack; *tc && *tc != c; tc = &(*tc)->nextInStack)
    ;
  *tc = c->nextInStack;

  if (c == c->monitor->active) {
    for (t = c->monitor->stack; t && !ISVISIBLE(t); t = t->nextInStack)
      ;
    c->monitor->active = t;
  }
}

Client *findClientFromWindow(Window w) {
  Client *c;
  Monitor *m;

  for (m = monitors; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

Client *getNextTiledWindow(Client *c) {
  for (; c && (c->isFloating || !ISVISIBLE(c)); c = c->next)
    ;
  return c;
}

void updateClientList(void) {
  Client *c;
  Monitor *m;

  XDeleteProperty(display, root, netAtoms[NET_CLIENT_LIST]);
  for (m = monitors; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(display, root, netAtoms[NET_CLIENT_LIST], XA_WINDOW, 32,
                      PropModeAppend, (unsigned char *)&(c->win), 1);
}
