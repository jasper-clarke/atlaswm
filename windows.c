// WINDOWS
// "How should this window appear and behave?"

#include "atlas.h"
#include "config.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

static const char broken[] = "BORKED";
/* HACK: Need to implement TOML config for these */
static const Rule rules[] = {
    /* xprop(1):
     *	WM_CLASS(STRING) = instance, class
     *	WM_NAME(STRING) = title
     */
    /* class      instance    title       tags mask     isfloating   monitor */
    {"Gimp", NULL, NULL, 0, 1, -1},
    {"Firefox", NULL, NULL, 1 << 8, 0, -1},
};
/* HACK: End of hack*/

void manage(Window w, XWindowAttributes *wa) {
  Client *c, *t = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldBorderWidth = wa->border_width;
  c->horizontalRatio = 0.5;
  c->verticalRatio = 0.5;

  if (XGetTransientForHint(display, w, &trans) &&
      (t = findClientFromWindow(trans))) {
    c->monitor = t->monitor;
    c->workspaces = t->workspaces;
  } else {
    c->monitor = selectedMonitor;
    applyWindowRules(c);
  }

  if (c->x + WIDTH(c) > c->monitor->wx + c->monitor->ww)
    c->x = c->monitor->wx + c->monitor->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->monitor->wy + c->monitor->wh)
    c->y = c->monitor->wy + c->monitor->wh - HEIGHT(c);
  c->x = MAX(c->x, c->monitor->wx);
  c->y = MAX(c->y, c->monitor->wy);
  c->borderWidth = cfg.borderWidth;

  wc.border_width = c->borderWidth;
  XConfigureWindow(display, w, CWBorderWidth, &wc);
  Clr borderColor;
  drw_clr_create(drawContext, &borderColor, cfg.borderInactiveColor);
  XSetWindowBorder(display, w, borderColor.pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updateWindowTypeProps(c);
  updateWindowSizeHints(c);
  updateWindowManagerHints(c);
  XSelectInput(display, w,
               EnterWindowMask | FocusChangeMask | PropertyChangeMask |
                   StructureNotifyMask);
  registerMouseButtons(c, 0);
  if (!c->isFloating)
    c->isFloating = c->previousState = trans != None || c->isFixedSize;
  if (c->isFloating)
    XRaiseWindow(display, c->win);
  attach(c);
  attachWindowToStack(c);
  XChangeProperty(display, root, netAtoms[NET_CLIENT_LIST], XA_WINDOW, 32,
                  PropModeAppend, (unsigned char *)&(c->win), 1);
  XMoveResizeWindow(display, c->win, c->x + 2 * screenWidth, c->y, c->w,
                    c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->monitor == selectedMonitor)
    unfocus(selectedMonitor->active, 0);
  c->monitor->active = c;
  arrange(c->monitor);
  XMapWindow(display, c->win);
  if (cfg.focusNewWindows) {
    focus(c);
    moveCursorToClientCenter(c);
  } else {
    focus(NULL);
  }
}

void unmanage(Client *c, int destroyed) {
  Monitor *m = c->monitor;
  XWindowChanges wc;

  Client *prev = NULL;
  Client *curr = m->clients;
  // Find the previous client
  while (curr && curr != c) {
    prev = curr;
    curr = curr->next;
  }

  detach(c);
  detachWindowFromStack(c);
  if (!destroyed) {
    wc.border_width = c->oldBorderWidth;
    XGrabServer(display); /* avoid race conditions */
    XSetErrorHandler(handleXErrorDummy);
    XSelectInput(display, c->win, NoEventMask);
    XConfigureWindow(display, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(display, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(display, False);
    XSetErrorHandler(handleXError);
    XUngrabServer(display);
  }
  free(c);

  focus(prev);
  updateClientList();
  arrange(m);
}

void updateWindowTitle(Client *c) {
  if (!gettextprop(c->win, netAtoms[NET_WM_NAME], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void updateWindowTypeProps(Client *c) {
  Atom state = getatomprop(c, netAtoms[NET_WM_STATE]);
  Atom wtype = getatomprop(c, netAtoms[NET_WM_WINDOW_TYPE]);

  if (state == netAtoms[NET_WM_FULLSCREEN])
    setWindowFullscreen(c, 1);
  if (wtype == netAtoms[NET_WM_WINDOW_TYPE_DIALOG])
    c->isFloating = 1;
}

void updateWindowManagerHints(Client *c) {
  XWMHints *wmh;

  if ((wmh = XGetWMHints(display, c->win))) {
    if (c == selectedMonitor->active && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(display, c->win, wmh);
    } else
      c->isUrgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverFocus = !wmh->input;
    else
      c->neverFocus = 0;
    XFree(wmh);
  }
}

void updateWindowSizeHints(Client *c) {
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(display, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->minAspectRatio = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxAspectRatio = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxAspectRatio = c->minAspectRatio = 0.0;
  c->isFixedSize =
      (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
  c->hintsvalid = 1;
}

void configure(Client *c) {
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = display;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->borderWidth;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(display, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void applyWindowRules(Client *c) {
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = {NULL, NULL};

  /* rule matching */
  c->isFloating = 0;
  c->workspaces = 0;
  XGetClassHint(display, c->win, &ch);
  class = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name ? ch.res_name : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title)) &&
        (!r->class || strstr(class, r->class)) &&
        (!r->instance || strstr(instance, r->instance))) {
      c->isFloating = r->isfloating;
      c->workspaces |= r->tags;
      for (m = monitors; m && m->num != r->monitor; m = m->next)
        ;
      if (m)
        c->monitor = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->workspaces =
      c->workspaces & WORKSPACEMASK
          ? c->workspaces & WORKSPACEMASK
          : c->monitor->workspaceset[c->monitor->selectedWorkspaces];
}

int applyWindowSizeConstraints(Client *c, int *x, int *y, int *w, int *h,
                               int interact) {
  int baseismin;
  Monitor *m = c->monitor;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > screenWidth)
      *x = screenWidth - WIDTH(c);
    if (*y > screenHeight)
      *y = screenHeight - HEIGHT(c);
    if (*x + *w + 2 * c->borderWidth < 0)
      *x = 0;
    if (*y + *h + 2 * c->borderWidth < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->borderWidth <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->borderWidth <= m->wy)
      *y = m->wy;
  }
  if (c->isFloating ||
      !c->monitor->layouts[c->monitor->selectedLayout]->arrange) {
    if (!c->hintsvalid)
      updateWindowSizeHints(c);
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->minAspectRatio > 0 && c->maxAspectRatio > 0) {
      if (c->maxAspectRatio < (float)*w / *h)
        *w = *h * c->maxAspectRatio + 0.5;
      else if (c->minAspectRatio < (float)*h / *w)
        *h = *w * c->minAspectRatio + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;
    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void setWindowFullscreen(Client *c, int fullscreen) {
  if (fullscreen && !c->isFullscreen) {
    XChangeProperty(display, c->win, netAtoms[NET_WM_STATE], XA_ATOM, 32,
                    PropModeReplace,
                    (unsigned char *)&netAtoms[NET_WM_FULLSCREEN], 1);
    c->isFullscreen = 1;
    c->previousState = c->isFloating;
    c->oldBorderWidth = c->borderWidth;
    c->borderWidth = 0;
    c->isFloating = 1;
    resizeclient(c, c->monitor->mx, c->monitor->my, c->monitor->mw,
                 c->monitor->mh);
    XRaiseWindow(display, c->win);
  } else if (!fullscreen && c->isFullscreen) {
    XChangeProperty(display, c->win, netAtoms[NET_WM_STATE], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)0, 0);
    c->isFullscreen = 0;
    c->isFloating = c->previousState;
    c->borderWidth = c->oldBorderWidth;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->monitor);
  }
}

void setWindowUrgent(Client *c, int urg) {
  XWMHints *wmh;

  c->isUrgent = urg;
  if (!(wmh = XGetWMHints(display, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(display, c->win, wmh);
  XFree(wmh);
}

void toggleWindowFloating(const Arg *arg) {
  if (!selectedMonitor->active)
    return;
  if (selectedMonitor->active
          ->isFullscreen) /* no support for fullscreen windows */
    return;
  selectedMonitor->active->isFloating = !selectedMonitor->active->isFloating ||
                                        selectedMonitor->active->isFixedSize;
  if (selectedMonitor->active->isFloating)
    resize(selectedMonitor->active, selectedMonitor->active->x,
           selectedMonitor->active->y, selectedMonitor->active->w,
           selectedMonitor->active->h, 0);
  arrange(selectedMonitor);
}

void toggleWindowVisibility(Client *c) {
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(display, c->win, c->x, c->y);
    if ((!c->monitor->layouts[c->monitor->selectedLayout]->arrange ||
         c->isFloating) &&
        !c->isFullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    toggleWindowVisibility(c->nextInStack);
  } else {
    /* hide clients bottom up */
    toggleWindowVisibility(c->nextInStack);
    XMoveWindow(display, c->win, WIDTH(c) * -2, c->y);
  }
}

void resize(Client *c, int x, int y, int w, int h, int interact) {
  if (applyWindowSizeConstraints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

void resizeclient(Client *c, int x, int y, int w, int h) {
  XWindowChanges wc;

  c->oldx = c->x;
  c->x = wc.x = x;
  c->oldy = c->y;
  c->y = wc.y = y;
  c->oldw = c->w;
  c->w = wc.width = w;
  c->oldh = c->h;
  c->h = wc.height = h;
  wc.border_width = c->borderWidth;
  XConfigureWindow(display, c->win,
                   CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
  configure(c);
  XSync(display, False);
}

void setclientstate(Client *c, long state) {
  long data[] = {state, None};

  XChangeProperty(display, c->win, wmAtoms[WM_STATE], wmAtoms[WM_STATE], 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

int sendevent(Client *c, Atom proto) {
  int n;
  Atom *protocols;
  int exists = 0;
  XEvent ev;

  if (XGetWMProtocols(display, c->win, &protocols, &n)) {
    while (!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wmAtoms[WM_PROTOCOLS];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, c->win, False, NoEventMask, &ev);
  }
  return exists;
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

Atom getatomprop(Client *c, Atom prop) {
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;

  if (XGetWindowProperty(display, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
                         &da, &di, &dl, &dl, &p) == Success &&
      p) {
    atom = *(Atom *)p;
    XFree(p);
  }
  return atom;
}

long getstate(Window w) {
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(display, w, wmAtoms[WM_STATE], 0L, 2L, False,
                         wmAtoms[WM_STATE], &real, &format, &n, &extra,
                         (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(display, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(display, &name, &list, &n) >= Success &&
             n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void setCurrentDesktop(void) {
  long data[] = {0};
  XChangeProperty(display, root, netAtoms[NET_CURRENT_DESKTOP], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}
void setDesktopNames(void) {
  XTextProperty text;
  // Create list of workspace names
  char **list = ecalloc(cfg.workspaceCount, sizeof(char *));
  for (size_t i = 0; i < cfg.workspaceCount; i++) {
    list[i] = strdup(cfg.workspaces[i].name);
  }

  // Convert list to text property
  Xutf8TextListToTextProperty(display, list, cfg.workspaceCount,
                              XUTF8StringStyle, &text);
  XSetTextProperty(display, root, &text, netAtoms[NET_DESKTOP_NAMES]);
}

void setNumDesktops(void) {
  long data[] = {cfg.workspaceCount};
  XChangeProperty(display, root, netAtoms[NET_NUMBER_OF_DESKTOPS], XA_CARDINAL,
                  32, PropModeReplace, (unsigned char *)data, 1);
}

void setViewport(void) {
  long data[] = {0, 0};
  XChangeProperty(display, root, netAtoms[NET_DESKTOP_VIEWPORT], XA_CARDINAL,
                  32, PropModeReplace, (unsigned char *)data, 2);
}

void updateCurrentDesktop(void) {
  long rawdata[] = {
      selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces]};
  int i = 0;
  while (*rawdata >> (i + 1)) {
    i++;
  }
  long data[] = {i};
  XChangeProperty(display, root, netAtoms[NET_CURRENT_DESKTOP], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}
