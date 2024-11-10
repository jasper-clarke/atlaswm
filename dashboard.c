// DASHBOARD

#include "atlas.h"
#include "configurer.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>

/* HACK: Need to implement TOML config for these */
static const char *tags[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
/* HACK: End of hack*/

void drawDash(Monitor *m) {
  int x, w, tw = 0;
  int boxs = drw->fonts->h / 9;
  int boxw = drw->fonts->h / 6 + 2;
  unsigned int i, occ = 0, urg = 0;
  Client *c;

  if (!m->showDash)
    return;

  /* draw status first so it can be overdrawn by tags later */
  if (m == selectedMonitor) { /* status is only drawn on selected monitor */
    drw_setscheme(drw, scheme[SchemeNorm]);
    tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
    drw_text(drw, m->ww - tw, 0, tw, bh, 0, stext, 0);
  }

  for (c = m->clients; c; c = c->next) {
    occ |= c->workspaces;
    if (c->isUrgent)
      urg |= c->workspaces;
  }
  x = 0;
  for (i = 0; i < LENGTH(tags); i++) {
    w = TEXTW(tags[i]);
    drw_setscheme(
        drw,
        scheme[m->workspaceset[m->selectedWorkspaces] & 1 << i ? SchemeSel
                                                               : SchemeNorm]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
    if (occ & 1 << i)
      drw_rect(drw, x + boxs, boxs, boxw, boxw,
               m == selectedMonitor && selectedMonitor->active &&
                   selectedMonitor->active->workspaces & 1 << i,
               urg & 1 << i);
    x += w;
  }
  w = TEXTW(m->layoutSymbol);
  drw_setscheme(drw, scheme[SchemeNorm]);
  x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->layoutSymbol, 0);

  if ((w = m->ww - tw - x) > bh) {
    if (m->active) {
      drw_setscheme(drw, scheme[m == selectedMonitor ? SchemeSel : SchemeNorm]);
      drw_text(drw, x, 0, w, bh, lrpad / 2, m->active->name, 0);
      if (m->active->isFloating)
        drw_rect(drw, x + boxs, boxs, boxw, boxw, m->active->isFixedSize, 0);
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
      drw_rect(drw, x, 0, w, bh, 1, 1);
    }
  }
  drw_map(drw, m->dashWin, 0, 0, m->ww, bh);
}

void drawDashes(void) {
  Monitor *m;

  for (m = monitors; m; m = m->next)
    drawDash(m);
}

void updateDashPosition(Monitor *m) {
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showDash) {
    m->wh -= bh;
    m->dashPos = m->dashPosTop ? m->wy : m->wy + m->wh;
    m->wy = m->dashPosTop ? m->wy + bh : m->wy;
  } else
    m->dashPos = -bh;
}

void updatebars(void) {
  Monitor *m;
  XSetWindowAttributes wa = {.override_redirect = True,
                             .background_pixmap = ParentRelative,
                             .event_mask = ButtonPressMask | ExposureMask};
  XClassHint ch = {"atlaswm", "atlaswm"};
  for (m = monitors; m; m = m->next) {
    if (m->dashWin)
      continue;
    m->dashWin = XCreateWindow(
        dpy, root, m->wx, m->dashPos, m->ww, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->dashWin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->dashWin);
    XSetClassHint(dpy, m->dashWin, &ch);
  }
}

void updatestatus(void) {
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "AtlasWM v" VERSION);
  drawDash(selectedMonitor);
}

void toggleDash(const Arg *arg) {
  selectedMonitor->showDash = !selectedMonitor->showDash;
  updateDashPosition(selectedMonitor);
  XMoveResizeWindow(dpy, selectedMonitor->dashWin, selectedMonitor->wx,
                    selectedMonitor->dashPos, selectedMonitor->ww, bh);
  arrange(selectedMonitor);
}
