#include "atlas.h"
#include "config.h"
#include <X11/Xatom.h>

void focus(Client *c) {
  if (!c || !ISVISIBLE(c))
    for (c = selectedMonitor->stack; c && !ISVISIBLE(c); c = c->nextInStack)
      ;
  if (selectedMonitor->active && selectedMonitor->active != c)
    unfocus(selectedMonitor->active, 0);
  if (c) {
    if (c->monitor != selectedMonitor)
      selectedMonitor = c->monitor;
    if (c->isUrgent)
      setWindowUrgent(c, 0);
    detachWindowFromStack(c);
    attachWindowToStack(c);
    registerMouseButtons(c, 1);
    Clr borderColor;
    drw_clr_create(drawContext, &borderColor, cfg.borderActiveColor);
    XSetWindowBorder(display, c->win, borderColor.pixel);
    setfocus(c);
  } else {
    XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(display, root, netAtoms[NET_ACTIVE_WINDOW]);
  }
  selectedMonitor->active = c;
}

void unfocus(Client *c, int setfocus) {
  if (!c)
    return;
  registerMouseButtons(c, 0);
  Clr borderColor;
  drw_clr_create(drawContext, &borderColor, cfg.borderInactiveColor);
  XSetWindowBorder(display, c->win, borderColor.pixel);
  if (setfocus) {
    XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(display, root, netAtoms[NET_ACTIVE_WINDOW]);
  }
}

void focusMonitor(const Arg *arg) {
  Monitor *m;

  if (!monitors->next)
    return;
  if ((m = findMonitorInDirection(arg->i)) == selectedMonitor)
    return;
  unfocus(selectedMonitor->active, 0);
  selectedMonitor = m;
  focus(NULL);
  moveCursorToClientCenter(selectedMonitor->active);
}

void focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selectedMonitor->active ||
      (selectedMonitor->active->isFullscreen && cfg.lockFullscreen))
    return;
  if (arg->i > 0) {
    for (c = selectedMonitor->active->next; c && !ISVISIBLE(c); c = c->next)
      ;
    if (!c)
      for (c = selectedMonitor->clients; c && !ISVISIBLE(c); c = c->next)
        ;
  } else {
    for (i = selectedMonitor->clients; i != selectedMonitor->active;
         i = i->next)
      if (ISVISIBLE(i))
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i))
          c = i;
  }
  if (c) {
    focus(c);
    moveCursorToClientCenter(c);
    restack(selectedMonitor);
  }
}

void setfocus(Client *c) {
  if (!c->neverFocus) {
    XSetInputFocus(display, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(display, root, netAtoms[NET_ACTIVE_WINDOW], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&(c->win), 1);
  }
  sendevent(c, wmAtoms[WM_TAKE_FOCUS]);
}

void moveCursorToClientCenter(Client *c) {
  if (!c || !cfg.moveCursorWithFocus)
    return;

  // Calculate center coordinates of the window
  int x = c->x + (c->w / 2);
  int y = c->y + (c->h / 2);

  // Move cursor to window center
  XWarpPointer(display, None, root, 0, 0, 0, 0, x, y);
  XFlush(display);
}
