#include "atlaswm.h"
#include "util.h"
#include <X11/Xlib.h>
#include <stdio.h>

void tile(Monitor *m) {
  unsigned int i, n, h, mw, my, ty;
  Client *c;

  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
    ;
  if (n == 0)
    return;

  if (n > m->nmaster)
    mw = m->nmaster ? m->ww * m->mfact : 0;
  else
    mw = m->ww;
  for (i = my = ty = 0, c = nexttiled(m->clients); c;
       c = nexttiled(c->next), i++)
    if (i < m->nmaster) {
      h = (m->wh - my) / (MIN(n, m->nmaster) - i);
      resize(c, m->wx, m->wy + my, mw - (2 * c->bw), h - (2 * c->bw), 0);
      if (my + HEIGHT(c) < m->wh)
        my += HEIGHT(c);
    } else {
      h = (m->wh - ty) / (n - i);
      resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2 * c->bw),
             h - (2 * c->bw), 0);
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
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void dwindle(Monitor *m) {
  Client *c;
  unsigned int n = 0;

  // Count visible clients
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    n++;

  if (n == 0)
    return;

  // Single window uses full space
  if (n == 1) {
    c = nexttiled(m->clients);
    resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
    return;
  }

  // Position for first window
  int x = m->wx;
  int y = m->wy;
  int w = m->ww;
  int h = m->wh;

  c = nexttiled(m->clients);
  int i = 0;

  while (c) {
    // Even numbers do vertical splits, odd do horizontal
    if (i % 2 == 0) {
      // Vertical split
      w = w / 2;
      resize(c, x, y, w - 2 * c->bw, h - 2 * c->bw, 0);
      x += w;
    } else {
      // Horizontal split
      h = h / 2;
      resize(c, x, y, w - 2 * c->bw, h - 2 * c->bw, 0);
      y += h;
    }

    c = nexttiled(c->next);
    i++;
  }
}

int shouldscale(Client *c) {
  return (c && !c->isfixed && !c->isfloating && !c->isfullscreen);
}

void scaleclient(Client *c, int x, int y, int w, int h, float scale) {
  if (!shouldscale(c))
    return;

  int new_w = w * scale;
  int new_h = h * scale;
  int new_x = x + (w - new_w) / 2;
  int new_y = y + (h - new_h) / 2;

  resize(c, new_x, new_y, new_w - 2 * c->bw, new_h - 2 * c->bw, 0);
}

void dwindlegaps(Monitor *m) {
  Client *c;
  unsigned int n = 0;
  const int gap = 10;        // Gap size between windows
  const int outer_gap = gap; // Gap from screen edges
  const float scale = 0.98;  // Slightly increased scale to reduce empty space

  // Count visible clients
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    n++;

  if (n == 0)
    return;

  // Calculate available space considering outer gaps
  int x = m->wx + outer_gap;
  int y = m->wy + outer_gap;
  int w = m->ww - (2 * outer_gap);
  int h = m->wh - (2 * outer_gap);

  // Single window case
  if (n == 1) {
    c = nexttiled(m->clients);
    resize(c, x, y, w - (2 * c->bw), h - (2 * c->bw), 0);
    return;
  }

  c = nexttiled(m->clients);
  int i = 0;
  int remaining_w = w;
  int remaining_h = h;

  while (c && c->next) { // Check for next window to properly calculate splits
    Client *next = nexttiled(c->next);
    if (!next)
      break; // Last window uses remaining space

    if (i % 2 == 0) {
      // Vertical split
      int new_w = (remaining_w - gap) / 2;
      resize(c, x, y, new_w - (2 * c->bw), remaining_h - (2 * c->bw), 0);
      x += new_w + gap;
      remaining_w = remaining_w - new_w - gap;
    } else {
      // Horizontal split
      int new_h = (remaining_h - gap) / 2;
      resize(c, x, y, remaining_w - (2 * c->bw), new_h - (2 * c->bw), 0);
      y += new_h + gap;
      remaining_h = remaining_h - new_h - gap;
    }

    c = next;
    i++;
  }

  // Last window uses remaining space
  if (c) {
    resize(c, x, y, remaining_w - (2 * c->bw), remaining_h - (2 * c->bw), 0);
  }
}
