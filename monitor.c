#include "atlas.h"
#include "configurer.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

const Layout layouts[] = {
    {"dwindle", dwindlegaps},
    {"floating", NULL},
    {"full", monocle},
};

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n,
                        XineramaScreenInfo *info) {
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
        unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

Monitor *createMonitor(void) {
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->workspaceset[0] = m->workspaceset[1] = 1;
  m->masterFactor = cfg.masterFactor;
  m->layouts[0] = &layouts[0];
  m->layouts[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->layoutSymbol, layouts[0].symbol, sizeof m->layoutSymbol);
  return m;
}

void cleanupMonitor(Monitor *mon) {
  Monitor *m;

  if (mon == monitors)
    monitors = monitors->next;
  else {
    for (m = monitors; m && m->next != mon; m = m->next)
      ;
    m->next = mon->next;
  }
  free(mon);
}

int updateMonitorGeometry(void) {
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = monitors; m; m = m->next, n++)
      ;
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;

    /* new monitors if nn > n */
    for (i = n; i < nn; i++) {
      for (m = monitors; m && m->next; m = m->next)
        ;
      if (m)
        m->next = createMonitor();
      else
        monitors = createMonitor();
    }
    for (i = 0, m = monitors; i < nn && m; m = m->next, i++)
      if (i >= n || unique[i].x_org != m->mx || unique[i].y_org != m->my ||
          unique[i].width != m->mw || unique[i].height != m->mh) {
        dirty = 1;
        m->num = i;
        m->mx = m->wx = unique[i].x_org;
        m->my = m->wy = unique[i].y_org;
        m->mw = m->ww = unique[i].width;
        m->mh = m->wh = unique[i].height;
      }
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = monitors; m && m->next; m = m->next)
        ;
      while ((c = m->clients)) {
        dirty = 1;
        m->clients = c->next;
        detachWindowFromStack(c);
        c->monitor = monitors;
        attach(c);
        attachWindowToStack(c);
      }
      if (m == selectedMonitor)
        selectedMonitor = monitors;
      cleanupMonitor(m);
    }
    free(unique);
  } else
#endif /* XINERAMA */
  {    /* default monitor setup */
    if (!monitors)
      monitors = createMonitor();
    if (monitors->mw != screenWidth || monitors->mh != screenHeight) {
      dirty = 1;
      monitors->mw = monitors->ww = screenWidth;
      monitors->mh = monitors->wh = screenHeight;
    }
  }
  if (dirty) {
    selectedMonitor = monitors;
    selectedMonitor = findMonitorFromWindow(root);
  }
  return dirty;
}

Monitor *findMonitorFromWindow(Window w) {
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getRootPointer(&x, &y))
    return getMonitorForArea(x, y, 1, 1);
  for (m = monitors; m; m = m->next)
    ;
  if ((c = findClientFromWindow(w)))
    return c->monitor;
  return selectedMonitor;
}

Monitor *findMonitorInDirection(int dir) {
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selectedMonitor->next))
      m = monitors;
  } else if (selectedMonitor == monitors)
    for (m = monitors; m->next; m = m->next)
      ;
  else
    for (m = monitors; m->next != selectedMonitor; m = m->next)
      ;
  return m;
}

Monitor *getMonitorForArea(int x, int y, int w, int h) {
  Monitor *m, *r = selectedMonitor;
  int a, area = 0;

  for (m = monitors; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void sendWindowToMonitor(Client *c, Monitor *m) {
  if (c->monitor == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachWindowFromStack(c);
  c->monitor = m;
  c->workspaces =
      m->workspaceset[m->selectedWorkspaces]; /* assign workspaces of target
                                                 monitor */
  attach(c);
  attachWindowToStack(c);
  focus(NULL);
  arrange(NULL);
}
