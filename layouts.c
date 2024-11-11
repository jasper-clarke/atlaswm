#include "atlas.h"
#include "configurer.h"
#include "util.h"
#include <X11/Xlib.h>
#include <stdio.h>

void arrange(Monitor *m) {
  if (m)
    toggleWindowVisibility(m->stack);
  else
    for (m = monitors; m; m = m->next)
      toggleWindowVisibility(m->stack);
  if (m) {
    arrangeMonitor(m);
    restack(m);
  } else
    for (m = monitors; m; m = m->next)
      arrangeMonitor(m);
}

void arrangeMonitor(Monitor *m) {
  safe_strcpy(m->layoutSymbol, m->layouts[m->selectedLayout]->symbol,
              sizeof m->layoutSymbol);
  if (m->layouts[m->selectedLayout]->arrange)
    m->layouts[m->selectedLayout]->arrange(m);
}

void setlayout(const Arg *arg) {
  if (!arg || !arg->v ||
      arg->v != selectedMonitor->layouts[selectedMonitor->selectedLayout])
    selectedMonitor->selectedLayout ^= 1;
  if (arg && arg->v)
    selectedMonitor->layouts[selectedMonitor->selectedLayout] =
        (Layout *)arg->v;
  safe_strcpy(selectedMonitor->layoutSymbol,
              selectedMonitor->layouts[selectedMonitor->selectedLayout]->symbol,
              sizeof selectedMonitor->layoutSymbol);
  if (selectedMonitor->active)
    arrange(selectedMonitor);
}

/* arg > 1.0 will set mfact absolutely */
void setMasterRatio(const Arg *arg) {
  float f;

  if (!arg ||
      !selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selectedMonitor->masterFactor : arg->f - 1.0;
  if (f < 0.05 || f > 0.95)
    return;
  selectedMonitor->masterFactor = f;
  arrange(selectedMonitor);
}

void monocle(Monitor *m) {
  unsigned int n = 0;
  Client *c;

  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      n++;
  if (n > 0) /* override layout symbol */
    snprintf(m->layoutSymbol, sizeof m->layoutSymbol, "[%d]", n);
  for (c = getNextTiledWindow(m->clients); c; c = getNextTiledWindow(c->next))
    resize(c, m->wx, m->wy, m->ww - 2 * c->borderWidth,
           m->wh - 2 * c->borderWidth, 0);
}

void dwindlegaps(Monitor *m) {
  Client *c;
  unsigned int n = 0;

  // Count visible clients
  for (c = getNextTiledWindow(m->clients); c; c = getNextTiledWindow(c->next))
    n++;

  if (n == 0)
    return;

  // Calculate available space considering outer gaps
  int x = m->wx + cfg.outerGaps;
  int y = m->wy + cfg.outerGaps;
  int w = m->ww - (2 * cfg.outerGaps);
  int h = m->wh - (2 * cfg.outerGaps);

  // Single window case
  if (n == 1) {
    c = getNextTiledWindow(m->clients);
    resize(c, x, y, w - (2 * c->borderWidth), h - (2 * c->borderWidth), 0);
    return;
  }

  c = getNextTiledWindow(m->clients);
  int i = 0;
  int remaining_w = w;
  int remaining_h = h;
  int current_x = x;
  int current_y = y;

  while (c) {
    Client *next = getNextTiledWindow(c->next);

    // Initialize ratios if not set
    if (c->horizontalRatio <= 0)
      c->horizontalRatio = 0.5;
    if (c->verticalRatio <= 0)
      c->verticalRatio = 0.5;

    if (!next) {
      // Last window uses remaining space
      resize(c, current_x, current_y, remaining_w - (2 * c->borderWidth),
             remaining_h - (2 * c->borderWidth), 0);
      break;
    }

    if (i % 2 == 0) {
      // Vertical split using horizontalRatio
      int new_w = (remaining_w - cfg.innerGaps) * c->horizontalRatio;
      resize(c, current_x, current_y, new_w - (2 * c->borderWidth),
             remaining_h - (2 * c->borderWidth), 0);
      current_x += new_w + cfg.innerGaps;
      remaining_w = remaining_w - new_w - cfg.innerGaps;
    } else {
      // Horizontal split using verticalRatio
      int new_h = (remaining_h - cfg.innerGaps) * c->verticalRatio;
      resize(c, current_x, current_y, remaining_w - (2 * c->borderWidth),
             new_h - (2 * c->borderWidth), 0);
      current_y += new_h + cfg.innerGaps;
      remaining_h = remaining_h - new_h - cfg.innerGaps;
    }

    c = next;
    i++;
  }
}

void restack(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  if (!m->active)
    return;
  if (m->active->isFloating || !m->layouts[m->selectedLayout]->arrange)
    XRaiseWindow(dpy, m->active->win);
  if (m->layouts[m->selectedLayout]->arrange) {
    wc.stack_mode = Below;
    for (c = m->stack; c; c = c->nextInStack)
      if (!c->isFloating && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
}
