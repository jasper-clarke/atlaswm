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
  else
    drawDash(selectedMonitor);
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

void incNumMasterWindows(const Arg *arg) {
  selectedMonitor->numMasterWindows =
      MAX(selectedMonitor->numMasterWindows + arg->i, 0);
  arrange(selectedMonitor);
}

void tile(Monitor *m) {
  unsigned int i, n, h, mw, my, ty;
  Client *c;

  for (n = 0, c = getNextTiledWindow(m->clients); c;
       c = getNextTiledWindow(c->next), n++)
    ;
  if (n == 0)
    return;

  if (n > m->numMasterWindows)
    mw = m->numMasterWindows ? m->ww * m->masterFactor : 0;
  else
    mw = m->ww;
  for (i = my = ty = 0, c = getNextTiledWindow(m->clients); c;
       c = getNextTiledWindow(c->next), i++)
    if (i < m->numMasterWindows) {
      h = (m->wh - my) / (MIN(n, m->numMasterWindows) - i);
      resize(c, m->wx, m->wy + my, mw - (2 * c->borderWidth),
             h - (2 * c->borderWidth), 0);
      if (my + HEIGHT(c) < m->wh)
        my += HEIGHT(c);
    } else {
      h = (m->wh - ty) / (n - i);
      resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2 * c->borderWidth),
             h - (2 * c->borderWidth), 0);
      if (ty + HEIGHT(c) < m->wh)
        ty += HEIGHT(c);
    }
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

void dwindle(Monitor *m) {
  Client *c;
  unsigned int n = 0;

  // Count visible clients
  for (c = getNextTiledWindow(m->clients); c; c = getNextTiledWindow(c->next))
    n++;

  if (n == 0)
    return;

  // Single window uses full space
  if (n == 1) {
    c = getNextTiledWindow(m->clients);
    resize(c, m->wx, m->wy, m->ww - 2 * c->borderWidth,
           m->wh - 2 * c->borderWidth, 0);
    return;
  }

  // Position for first window
  int x = m->wx;
  int y = m->wy;
  int w = m->ww;
  int h = m->wh;

  c = getNextTiledWindow(m->clients);
  int i = 0;

  while (c) {
    // Even numbers do vertical splits, odd do horizontal
    if (i % 2 == 0) {
      // Vertical split
      w = w / 2;
      resize(c, x, y, w - 2 * c->borderWidth, h - 2 * c->borderWidth, 0);
      x += w;
    } else {
      // Horizontal split
      h = h / 2;
      resize(c, x, y, w - 2 * c->borderWidth, h - 2 * c->borderWidth, 0);
      y += h;
    }

    c = getNextTiledWindow(c->next);
    i++;
  }
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
