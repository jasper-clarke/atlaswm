/*
 * AtlasWM is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of AtlasWM are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "atlas.h"
#include "ipc.h"
#include "util.h"

/* macros */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)                                                        \
  (mask & ~(numlockmask | LockMask) &                                          \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |      \
    Mod5Mask))
#define INTERSECT(x, y, w, h, m)                                               \
  (MAX(0, MIN((x) + (w), (m)->wx + (m)->ww) - MAX((x), (m)->wx)) *             \
   MAX(0, MIN((y) + (h), (m)->wy + (m)->wh) - MAX((y), (m)->wy)))
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define TAGMASK ((1 << LENGTH(tags)) - 1)
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)

/* variables */
static const int resizehints = 0; // 1 means tiling layouts break
static const char broken[] = "BORKED";
static char stext[256];
static int screen;
static int screenWidth,
    screenHeight; /* X display screen geometry width, height */
static int bh;    /* bar height */
static int lrpad; /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent])(XEvent *) = {
    [ButtonPress] = handleMouseButtonPress,
    [ClientMessage] = handleClientMessage,
    [ConfigureRequest] = handleConfigureRequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = handleWindowDestroy,
    [EnterNotify] = handleMouseEnter,
    [Expose] = handleExpose,
    [FocusIn] = handleFocusIn,
    [KeyPress] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = handleMouseMotion,
    [PropertyNotify] = handlePropertyChange,
    [UnmapNotify] = handleWindowUnmap};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *monitors, *selectedMonitor;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
  char limitexceeded[LENGTH(tags) > 31 ? -1 : 1];
};

/* function implementations */
void applyWindowRules(Client *c) {
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = {NULL, NULL};

  /* rule matching */
  c->isFloating = 0;
  c->tags = 0;
  XGetClassHint(dpy, c->win, &ch);
  class = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name ? ch.res_name : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title)) &&
        (!r->class || strstr(class, r->class)) &&
        (!r->instance || strstr(instance, r->instance))) {
      c->isFloating = r->isfloating;
      c->tags |= r->tags;
      for (m = monitors; m && m->num != r->monitor; m = m->next)
        ;
      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->tags = c->tags & TAGMASK ? c->tags & TAGMASK
                              : c->mon->tagset[c->mon->selectedTags];
}

int applyWindowSizeConstraints(Client *c, int *x, int *y, int *w, int *h,
                               int interact) {
  int baseismin;
  Monitor *m = c->mon;

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
  if (*h < bh)
    *h = bh;
  if (*w < bh)
    *w = bh;
  if (resizehints || c->isFloating ||
      !c->mon->layouts[c->mon->selectedLayout]->arrange) {
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
  strncpy(m->ltsymbol, m->layouts[m->selectedLayout]->symbol,
          sizeof m->ltsymbol);
  if (m->layouts[m->selectedLayout]->arrange)
    m->layouts[m->selectedLayout]->arrange(m);
}

void attach(Client *c) {
  c->next = c->mon->clients;
  c->mon->clients = c;
}

void attachWindowToStack(Client *c) {
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void handleMouseButtonPress(XEvent *e) {
  unsigned int i, x, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = findMonitorFromWindow(ev->window)) && m != selectedMonitor) {
    unfocus(selectedMonitor->sel, 1);
    selectedMonitor = m;
    focus(NULL);
  }
  if (ev->window == selectedMonitor->barwin) {
    i = x = 0;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
    } else if (ev->x < x + TEXTW(selectedMonitor->ltsymbol))
      click = ClkLtSymbol;
    else if (ev->x > selectedMonitor->ww - (int)TEXTW(stext))
      click = ClkStatusText;
    else
      click = ClkWinTitle;
  } else if ((c = findClientFromWindow(ev->window))) {
    focus(c);
    restack(selectedMonitor);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func &&
        buttons[i].button == ev->button &&
        CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(
          click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void checkForOtherWM(void) {
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void cleanup(void) {
  Arg a = {.ui = ~0};
  Layout foo = {"", NULL};
  Monitor *m;
  size_t i;

  view(&a);
  selectedMonitor->layouts[selectedMonitor->selectedLayout] = &foo;
  for (m = monitors; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, 0);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (monitors)
    cleanupMonitor(monitors);
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  free(scheme);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
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
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

void handleClientMessage(XEvent *e) {
  XClientMessageEvent *cme = &e->xclient;
  Client *c = findClientFromWindow(cme->window);

  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen] ||
        cme->data.l[2] == netatom[NetWMFullscreen])
      setWindowFullscreen(c,
                          (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                           || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                               !c->isFullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selectedMonitor->sel && !c->isUrgent)
      setWindowUrgent(c, 1);
  }
}

void configure(Client *c) {
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->borderWidth;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void configurenotify(XEvent *e) {
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updateMonitorGeometry handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (screenWidth != ev->width || screenHeight != ev->height);
    screenWidth = ev->width;
    screenHeight = ev->height;
    if (updateMonitorGeometry() || dirty) {
      drw_resize(drw, screenWidth, bh);
      updatebars();
      for (m = monitors; m; m = m->next) {
        for (c = m->clients; c; c = c->next)
          if (c->isFullscreen)
            resizeclient(c, m->mx, m->my, m->mw, m->mh);
        XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

void handleConfigureRequest(XEvent *e) {
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = findClientFromWindow(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->borderWidth = ev->border_width;
    else if (c->isFloating ||
             !selectedMonitor->layouts[selectedMonitor->selectedLayout]
                  ->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
        c->oldx = c->x;
        c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
        c->oldy = c->y;
        c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
        c->oldw = c->w;
        c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
        c->oldh = c->h;
        c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isFloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isFloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX | CWY)) &&
          !(ev->value_mask & (CWWidth | CWHeight)))
        configure(c);
      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *createMonitor(void) {
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->tagset[0] = m->tagset[1] = 1;
  m->masterFactor = mfact;
  m->numMasterWindows = nmaster;
  m->showbar = showbar;
  m->topbar = topbar;
  m->layouts[0] = &layouts[0];
  m->layouts[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
  return m;
}

void handleWindowDestroy(XEvent *e) {
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = findClientFromWindow(ev->window)))
    unmanage(c, 1);
}

void detach(Client *c) {
  Client **tc;

  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next)
    ;
  *tc = c->next;
}

void detachWindowFromStack(Client *c) {
  Client **tc, *t;

  for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext)
    ;
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext)
      ;
    c->mon->sel = t;
  }
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

void drawDash(Monitor *m) {
  int x, w, tw = 0;
  int boxs = drw->fonts->h / 9;
  int boxw = drw->fonts->h / 6 + 2;
  unsigned int i, occ = 0, urg = 0;
  Client *c;

  if (!m->showbar)
    return;

  /* draw status first so it can be overdrawn by tags later */
  if (m == selectedMonitor) { /* status is only drawn on selected monitor */
    drw_setscheme(drw, scheme[SchemeNorm]);
    tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
    drw_text(drw, m->ww - tw, 0, tw, bh, 0, stext, 0);
  }

  for (c = m->clients; c; c = c->next) {
    occ |= c->tags;
    if (c->isUrgent)
      urg |= c->tags;
  }
  x = 0;
  for (i = 0; i < LENGTH(tags); i++) {
    w = TEXTW(tags[i]);
    drw_setscheme(
        drw,
        scheme[m->tagset[m->selectedTags] & 1 << i ? SchemeSel : SchemeNorm]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
    if (occ & 1 << i)
      drw_rect(drw, x + boxs, boxs, boxw, boxw,
               m == selectedMonitor && selectedMonitor->sel &&
                   selectedMonitor->sel->tags & 1 << i,
               urg & 1 << i);
    x += w;
  }
  w = TEXTW(m->ltsymbol);
  drw_setscheme(drw, scheme[SchemeNorm]);
  x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

  if ((w = m->ww - tw - x) > bh) {
    if (m->sel) {
      drw_setscheme(drw, scheme[m == selectedMonitor ? SchemeSel : SchemeNorm]);
      drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
      if (m->sel->isFloating)
        drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isFixedSize, 0);
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
      drw_rect(drw, x, 0, w, bh, 1, 1);
    }
  }
  drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void drawDashes(void) {
  Monitor *m;

  for (m = monitors; m; m = m->next)
    drawDash(m);
}

void handleMouseEnter(XEvent *e) {
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
      ev->window != root)
    return;
  c = findClientFromWindow(ev->window);
  m = c ? c->mon : findMonitorFromWindow(ev->window);
  if (m != selectedMonitor) {
    unfocus(selectedMonitor->sel, 1);
    selectedMonitor = m;
  } else if (!c || c == selectedMonitor->sel)
    return;
  focus(c);
}

void handleExpose(XEvent *e) {
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = findMonitorFromWindow(ev->window)))
    drawDash(m);
}

void focus(Client *c) {
  if (!c || !ISVISIBLE(c))
    for (c = selectedMonitor->stack; c && !ISVISIBLE(c); c = c->snext)
      ;
  if (selectedMonitor->sel && selectedMonitor->sel != c)
    unfocus(selectedMonitor->sel, 0);
  if (c) {
    if (c->mon != selectedMonitor)
      selectedMonitor = c->mon;
    if (c->isUrgent)
      setWindowUrgent(c, 0);
    detachWindowFromStack(c);
    attachWindowToStack(c);
    registerMouseButtons(c, 1);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selectedMonitor->sel = c;
  drawDashes();
}

/* there are some broken focus acquiring clients needing extra handling */
void handleFocusIn(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (selectedMonitor->sel && ev->window != selectedMonitor->sel->win)
    setfocus(selectedMonitor->sel);
}

void focusMonitor(const Arg *arg) {
  Monitor *m;

  if (!monitors->next)
    return;
  if ((m = findMonitorInDirection(arg->i)) == selectedMonitor)
    return;
  unfocus(selectedMonitor->sel, 0);
  selectedMonitor = m;
  focus(NULL);
}

void focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selectedMonitor->sel ||
      (selectedMonitor->sel->isFullscreen && lockfullscreen))
    return;
  if (arg->i > 0) {
    for (c = selectedMonitor->sel->next; c && !ISVISIBLE(c); c = c->next)
      ;
    if (!c)
      for (c = selectedMonitor->clients; c && !ISVISIBLE(c); c = c->next)
        ;
  } else {
    for (i = selectedMonitor->clients; i != selectedMonitor->sel; i = i->next)
      if (ISVISIBLE(i))
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i))
          c = i;
  }
  if (c) {
    focus(c);
    restack(selectedMonitor);
  }
}

Atom getatomprop(Client *c, Atom prop) {
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
                         &da, &di, &dl, &dl, &p) == Success &&
      p) {
    atom = *(Atom *)p;
    XFree(p);
  }
  return atom;
}

int getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False,
                         wmatom[WMState], &real, &format, &n, &extra,
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
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success &&
             n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void registerMouseButtons(Client *c, int focused) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False, BUTTONMASK,
                  GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabButton(dpy, buttons[i].button, buttons[i].mask | modifiers[j],
                      c->win, False, BUTTONMASK, GrabModeAsync, GrabModeSync,
                      None, None);
  }
}

void registerKeyboardShortcuts(void) {
  updatenumlockmask();
  {
    unsigned int i, j, k;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};
    int start, end, skip;
    KeySym *syms;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XDisplayKeycodes(dpy, &start, &end);
    syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
    if (!syms)
      return;
    for (k = start; k <= end; k++)
      for (i = 0; i < LENGTH(keys); i++)
        /* skip modifier codes, we do that ourselves */
        if (keys[i].keysym == syms[(k - start) * skip])
          for (j = 0; j < LENGTH(modifiers); j++)
            XGrabKey(dpy, k, keys[i].mod | modifiers[j], root, True,
                     GrabModeAsync, GrabModeAsync);
    XFree(syms);
  }
}

void incNumMasterWindows(const Arg *arg) {
  selectedMonitor->numMasterWindows =
      MAX(selectedMonitor->numMasterWindows + arg->i, 0);
  arrange(selectedMonitor);
}

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

void keypress(XEvent *e) {
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  // TODO: Deprecated
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  for (i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym &&
        CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
      keys[i].func(&(keys[i].arg));
}

void killclient(const Arg *arg) {
  if (!selectedMonitor->sel)
    return;
  if (!sendevent(selectedMonitor->sel, wmatom[WMDelete])) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selectedMonitor->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

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

  updateWindowTitle(c);
  if (XGetTransientForHint(dpy, w, &trans) &&
      (t = findClientFromWindow(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  } else {
    c->mon = selectedMonitor;
    applyWindowRules(c);
  }

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);
  c->borderWidth = borderpx;

  wc.border_width = c->borderWidth;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updateWindowTypeProps(c);
  updateWindowSizeHints(c);
  updateWindowManagerHints(c);
  XSelectInput(dpy, w,
               EnterWindowMask | FocusChangeMask | PropertyChangeMask |
                   StructureNotifyMask);
  registerMouseButtons(c, 0);
  if (!c->isFloating)
    c->isFloating = c->previousState = trans != None || c->isFixedSize;
  if (c->isFloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachWindowToStack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                  PropModeAppend, (unsigned char *)&(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * screenWidth, c->y, c->w,
                    c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->mon == selectedMonitor)
    unfocus(selectedMonitor->sel, 0);
  c->mon->sel = c;
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  focus(NULL);
}

void mappingnotify(XEvent *e) {
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    registerKeyboardShortcuts();
}

void maprequest(XEvent *e) {
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!findClientFromWindow(ev->window))
    manage(ev->window, &wa);
}

void handleMouseMotion(XEvent *e) {
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = getMonitorForArea(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selectedMonitor->sel, 1);
    selectedMonitor = m;
    focus(NULL);
  }
  mon = m;
}

void movemouse(const Arg *arg) {
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selectedMonitor->sel))
    return;
  if (c->isFullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selectedMonitor);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);
      if (abs(selectedMonitor->wx - nx) < snap)
        nx = selectedMonitor->wx;
      else if (abs((selectedMonitor->wx + selectedMonitor->ww) -
                   (nx + WIDTH(c))) < snap)
        nx = selectedMonitor->wx + selectedMonitor->ww - WIDTH(c);
      if (abs(selectedMonitor->wy - ny) < snap)
        ny = selectedMonitor->wy;
      else if (abs((selectedMonitor->wy + selectedMonitor->wh) -
                   (ny + HEIGHT(c))) < snap)
        ny = selectedMonitor->wy + selectedMonitor->wh - HEIGHT(c);
      if (!c->isFloating &&
          selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange &&
          (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
        toggleWindowFloating(NULL);
      if (!selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange ||
          c->isFloating)
        resize(c, nx, ny, c->w, c->h, 1);
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = getMonitorForArea(c->x, c->y, c->w, c->h)) != selectedMonitor) {
    sendWindowToMonitor(c, m);
    selectedMonitor = m;
    focus(NULL);
  }
}

Client *getNextTiledWindow(Client *c) {
  for (; c && (c->isFloating || !ISVISIBLE(c)); c = c->next)
    ;
  return c;
}

void pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void handlePropertyChange(XEvent *e) {
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((ev->window == root) && (ev->atom == command_atom)) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, root, command_atom, 0, 1, True, XA_CARDINAL,
                           &type, &format, &nitems, &bytes_after,
                           &data) == Success) {
      if (data) {
        CommandType cmd = *(CommandType *)data;
        handle_command(cmd);
        XFree(data);
      }
    }
  } else if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = findClientFromWindow(ev->window))) {
    switch (ev->atom) {
    default:
      break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isFloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
          (c->isFloating = (findClientFromWindow(trans)) != NULL))
        arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      c->hintsvalid = 0;
      break;
    case XA_WM_HINTS:
      updateWindowManagerHints(c);
      drawDashes();
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updateWindowTitle(c);
      if (c == c->mon->sel)
        drawDash(c->mon);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updateWindowTypeProps(c);
  }
}

void quit(const Arg *arg) { running = 0; }

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
  XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                   &wc);
  configure(c);
  XSync(dpy, False);
}

void resizemouse(const Arg *arg) {
  int ocx, ocy, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selectedMonitor->sel))
    return;
  if (c->isFullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selectedMonitor);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->borderWidth - 1,
               c->h + c->borderWidth - 1);
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      nw = MAX(ev.xmotion.x - ocx - 2 * c->borderWidth + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->borderWidth + 1, 1);
      if (c->mon->wx + nw >= selectedMonitor->wx &&
          c->mon->wx + nw <= selectedMonitor->wx + selectedMonitor->ww &&
          c->mon->wy + nh >= selectedMonitor->wy &&
          c->mon->wy + nh <= selectedMonitor->wy + selectedMonitor->wh) {
        if (!c->isFloating &&
            selectedMonitor->layouts[selectedMonitor->selectedLayout]
                ->arrange &&
            (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
          toggleWindowFloating(NULL);
      }
      if (!selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange ||
          c->isFloating)
        resize(c, c->x, c->y, nw, nh, 1);
      break;
    }
  } while (ev.type != ButtonRelease);
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->borderWidth - 1,
               c->h + c->borderWidth - 1);
  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
  if ((m = getMonitorForArea(c->x, c->y, c->w, c->h)) != selectedMonitor) {
    sendWindowToMonitor(c, m);
    selectedMonitor = m;
    focus(NULL);
  }
}

void restack(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawDash(m);
  if (!m->sel)
    return;
  if (m->sel->isFloating || !m->layouts[m->selectedLayout]->arrange)
    XRaiseWindow(dpy, m->sel->win);
  if (m->layouts[m->selectedLayout]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isFloating && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
}

void run(void) {
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev))
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
}

void scan(void) {
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect ||
          XGetTransientForHint(dpy, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;
      if (XGetTransientForHint(dpy, wins[i], &d1) &&
          (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void sendWindowToMonitor(Client *c, Monitor *m) {
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachWindowFromStack(c);
  c->mon = m;
  c->tags = m->tagset[m->selectedTags]; /* assign tags of target monitor */
  attach(c);
  attachWindowToStack(c);
  focus(NULL);
  arrange(NULL);
}

void setclientstate(Client *c, long state) {
  long data[] = {state, None};

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

int sendevent(Client *c, Atom proto) {
  int n;
  Atom *protocols;
  int exists = 0;
  XEvent ev;

  if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
    while (!exists && n--)
      exists = protocols[n] == proto;
    XFree(protocols);
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wmatom[WMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  }
  return exists;
}

void setfocus(Client *c) {
  if (!c->neverFocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&(c->win), 1);
  }
  sendevent(c, wmatom[WMTakeFocus]);
}

void setWindowFullscreen(Client *c, int fullscreen) {
  if (fullscreen && !c->isFullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen],
                    1);
    c->isFullscreen = 1;
    c->previousState = c->isFloating;
    c->oldBorderWidth = c->borderWidth;
    c->borderWidth = 0;
    c->isFloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isFullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)0, 0);
    c->isFullscreen = 0;
    c->isFloating = c->previousState;
    c->borderWidth = c->oldBorderWidth;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void setlayout(const Arg *arg) {
  if (!arg || !arg->v ||
      arg->v != selectedMonitor->layouts[selectedMonitor->selectedLayout])
    selectedMonitor->selectedLayout ^= 1;
  if (arg && arg->v)
    selectedMonitor->layouts[selectedMonitor->selectedLayout] =
        (Layout *)arg->v;
  strncpy(selectedMonitor->ltsymbol,
          selectedMonitor->layouts[selectedMonitor->selectedLayout]->symbol,
          sizeof selectedMonitor->ltsymbol);
  if (selectedMonitor->sel)
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

// Function to run list of programs at startup
void startupPrograms() {
  pid_t pid;
  const char **args;
  int i;

  if (!exec)
    return;

  /* iterate through startup programs */
  for (i = 0; exec[i];) {
    /* Find the command and its arguments */
    args = &exec[i];

    /* Count arguments to find next command */
    while (exec[i])
      i++;
    i++; /* Skip the NULL terminator */

    /* Create new process */
    if ((pid = fork()) == -1) {
      LOG_ERROR("Failed to fork for '%s': %s", args[0], strerror(errno));
      continue;
    }

    /* Child process */
    if (pid == 0) {
      /* Close X connection in child */
      if (dpy)
        close(ConnectionNumber(dpy));

      /* Create new session */
      if (setsid() == -1) {
        LOG_ERROR("setsid failed for '%s': %s", args[0], strerror(errno));
        exit(EXIT_FAILURE);
      }

      /* Execute the program with its arguments */
      execvp(args[0], (char *const *)args);

      /* If we get here, execvp failed */
      LOG_ERROR("Failed to execute '%s': %s", args[0], strerror(errno));
      exit(EXIT_FAILURE);
    }

    /* Parent process */
    LOG_INFO("Started program: %s (pid: %d)", args[0], pid);
  }
}

void setup(void) {
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sigaction sa;

  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;

  /* init screen */
  screen = DefaultScreen(dpy);
  screenWidth = DisplayWidth(dpy, screen);
  screenHeight = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  drw = drw_create(dpy, screen, root, screenWidth, screenHeight);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    LOG_FATAL("No fonts could be loaded");
  lrpad = drw->fonts->h;
  bh = drw->fonts->h + 2;
  updateMonitorGeometry();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] =
      XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] =
      XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] = drw_cur_create(drw, XC_sizing);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);
  /* init bars */
  updatebars();
  setup_ipc(dpy);
  updatestatus();
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
                  PropModeReplace, (unsigned char *)"atlaswm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)netatom, NetLast);
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                  ButtonPressMask | PointerMotionMask | EnterWindowMask |
                  LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  registerKeyboardShortcuts();
  focus(NULL);
  startupPrograms();
}

void setWindowUrgent(Client *c, int urg) {
  XWMHints *wmh;

  c->isUrgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void toggleWindowVisibility(Client *c) {
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->layouts[c->mon->selectedLayout]->arrange || c->isFloating) &&
        !c->isFullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    toggleWindowVisibility(c->snext);
  } else {
    /* hide clients bottom up */
    toggleWindowVisibility(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void spawn(const Arg *arg) {
  struct sigaction sa;

  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    execvp(((char **)arg->v)[0], (char **)arg->v);
    LOG_ERROR("Failed to execute '%s'", ((char **)arg->v)[0]);
  }
}

void tag(const Arg *arg) {
  if (selectedMonitor->sel && arg->ui & TAGMASK) {
    selectedMonitor->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void directWindowToMonitor(const Arg *arg) {
  if (!selectedMonitor->sel || !monitors->next)
    return;
  sendWindowToMonitor(selectedMonitor->sel, findMonitorInDirection(arg->i));
}

void toggleDash(const Arg *arg) {
  selectedMonitor->showbar = !selectedMonitor->showbar;
  updateDashPosition(selectedMonitor);
  XMoveResizeWindow(dpy, selectedMonitor->barwin, selectedMonitor->wx,
                    selectedMonitor->by, selectedMonitor->ww, bh);
  arrange(selectedMonitor);
}

void toggleWindowFloating(const Arg *arg) {
  if (!selectedMonitor->sel)
    return;
  if (selectedMonitor->sel
          ->isFullscreen) /* no support for fullscreen windows */
    return;
  selectedMonitor->sel->isFloating =
      !selectedMonitor->sel->isFloating || selectedMonitor->sel->isFixedSize;
  if (selectedMonitor->sel->isFloating)
    resize(selectedMonitor->sel, selectedMonitor->sel->x,
           selectedMonitor->sel->y, selectedMonitor->sel->w,
           selectedMonitor->sel->h, 0);
  arrange(selectedMonitor);
}

void toggletag(const Arg *arg) {
  unsigned int newtags;

  if (!selectedMonitor->sel)
    return;
  newtags = selectedMonitor->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    selectedMonitor->sel->tags = newtags;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void toggleview(const Arg *arg) {
  unsigned int newtagset =
      selectedMonitor->tagset[selectedMonitor->selectedTags] ^
      (arg->ui & TAGMASK);

  if (newtagset) {
    selectedMonitor->tagset[selectedMonitor->selectedTags] = newtagset;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void unfocus(Client *c, int setfocus) {
  if (!c)
    return;
  registerMouseButtons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void unmanage(Client *c, int destroyed) {
  Monitor *m = c->mon;
  XWindowChanges wc;

  detach(c);
  detachWindowFromStack(c);
  if (!destroyed) {
    wc.border_width = c->oldBorderWidth;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XSelectInput(dpy, c->win, NoEventMask);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);
  focus(NULL);
  updateclientlist();
  arrange(m);
}

void handleWindowUnmap(XEvent *e) {
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = findClientFromWindow(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, 0);
  }
}

void updatebars(void) {
  Monitor *m;
  XSetWindowAttributes wa = {.override_redirect = True,
                             .background_pixmap = ParentRelative,
                             .event_mask = ButtonPressMask | ExposureMask};
  XClassHint ch = {"atlaswm", "atlaswm"};
  for (m = monitors; m; m = m->next) {
    if (m->barwin)
      continue;
    m->barwin = XCreateWindow(
        dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->barwin);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void updateDashPosition(Monitor *m) {
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar) {
    m->wh -= bh;
    m->by = m->topbar ? m->wy : m->wy + m->wh;
    m->wy = m->topbar ? m->wy + bh : m->wy;
  } else
    m->by = -bh;
}

void updateclientlist(void) {
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = monitors; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                      PropModeAppend, (unsigned char *)&(c->win), 1);
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
        updateDashPosition(m);
      }
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = monitors; m && m->next; m = m->next)
        ;
      while ((c = m->clients)) {
        dirty = 1;
        m->clients = c->next;
        detachWindowFromStack(c);
        c->mon = monitors;
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
      updateDashPosition(monitors);
    }
  }
  if (dirty) {
    selectedMonitor = monitors;
    selectedMonitor = findMonitorFromWindow(root);
  }
  return dirty;
}

void updatenumlockmask(void) {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
          XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void updateWindowSizeHints(Client *c) {
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
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

void updatestatus(void) {
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "AtlasWM v" VERSION);
  drawDash(selectedMonitor);
}

void updateWindowTitle(Client *c) {
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void updateWindowTypeProps(Client *c) {
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setWindowFullscreen(c, 1);
  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isFloating = 1;
}

void updateWindowManagerHints(Client *c) {
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selectedMonitor->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
      c->isUrgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverFocus = !wmh->input;
    else
      c->neverFocus = 0;
    XFree(wmh);
  }
}

void view(const Arg *arg) {
  if ((arg->ui & TAGMASK) ==
      selectedMonitor->tagset[selectedMonitor->selectedTags])
    return;
  selectedMonitor->selectedTags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK)
    selectedMonitor->tagset[selectedMonitor->selectedTags] = arg->ui & TAGMASK;
  focus(NULL);
  arrange(selectedMonitor);
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

Monitor *findMonitorFromWindow(Window w) {
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return getMonitorForArea(x, y, 1, 1);
  for (m = monitors; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = findClientFromWindow(w)))
    return c->mon;
  return selectedMonitor;
}

//
/* Error handling */
//

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == BadWindow ||
      (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
      (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolyFillRectangle &&
       ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
      (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
      (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
      (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
      (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  LOG_ERROR("Xerror: request_code=%d, error_code=%d", ee->request_code,
            ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }

/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
  LOG_FATAL("Another window manager is already running");
  return -1;
}

void zoom(const Arg *arg) {
  Client *c = selectedMonitor->sel;

  if (!selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange ||
      !c || c->isFloating)
    return;
  if (c == getNextTiledWindow(selectedMonitor->clients) &&
      !(c = getNextTiledWindow(c->next)))
    return;
  pop(c);
}

int main(int argc, char *argv[]) {
  if (argc == 2) {
    if (!strcmp("-v", argv[1])) {
      die("atlaswm-" VERSION);
    } else if (strcmp(argv[1], "reload") == 0) {
      Display *d = XOpenDisplay(NULL);
      if (!d) {
        LOG_ERROR("Cannot open display");
        return 1;
      }

      int success = send_command(d, CMD_RELOAD);
      XCloseDisplay(d);
      return success ? 0 : 1;

    } else {
      die("Usage: atlaswm [-v|reload]");
    }
  } else if (argc != 1)
    die("Usage: atlaswm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    LOG_FATAL("No locale support");
  if (!(dpy = XOpenDisplay(NULL)))
    LOG_FATAL("Failed to open display");
  checkForOtherWM();
  LOG_INFO("AtlasWM starting");
  setup();
  LOG_INFO("AtlasWM setup complete");
  scan();
  run();
  LOG_INFO("AtlasWM is exiting");
  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
