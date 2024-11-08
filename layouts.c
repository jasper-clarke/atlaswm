#include "atlas.h"
#include "config.h"
#include "util.h"
#include <X11/Xlib.h>
#include <stdio.h>

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
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
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

int shouldscale(Client *c) {
  return (c && !c->isFixedSize && !c->isFloating && !c->isFullscreen);
}

void scaleclient(Client *c, int x, int y, int w, int h, float scale) {
  if (!shouldscale(c))
    return;

  int new_w = w * scale;
  int new_h = h * scale;
  int new_x = x + (w - new_w) / 2;
  int new_y = y + (h - new_h) / 2;

  resize(c, new_x, new_y, new_w - 2 * c->borderWidth,
         new_h - 2 * c->borderWidth, 0);
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
  int x = m->wx + OUTERGAPS;
  int y = m->wy + OUTERGAPS;
  int w = m->ww - (2 * OUTERGAPS);
  int h = m->wh - (2 * OUTERGAPS);

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

  while (c && c->next) { // Check for next window to properly calculate splits
    Client *next = getNextTiledWindow(c->next);
    if (!next)
      break; // Last window uses remaining space

    if (i % 2 == 0) {
      // Vertical split
      int new_w = (remaining_w - INNERGAPS) / 2;
      resize(c, x, y, new_w - (2 * c->borderWidth),
             remaining_h - (2 * c->borderWidth), 0);
      x += new_w + INNERGAPS;
      remaining_w = remaining_w - new_w - INNERGAPS;
    } else {
      // Horizontal split
      int new_h = (remaining_h - INNERGAPS) / 2;
      resize(c, x, y, remaining_w - (2 * c->borderWidth),
             new_h - (2 * c->borderWidth), 0);
      y += new_h + INNERGAPS;
      remaining_h = remaining_h - new_h - INNERGAPS;
    }

    c = next;
    i++;
  }

  // Last window uses remaining space
  if (c) {
    resize(c, x, y, remaining_w - (2 * c->borderWidth),
           remaining_h - (2 * c->borderWidth), 0);
  }
}
