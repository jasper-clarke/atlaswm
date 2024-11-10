// EVENTS
// "How should this xserver event be handled?"

#include "atlas.h"
#include "configurer.h"
#include "ipc.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

/* HACK: Need to implement TOML config for these */
/* tagging */
static const char *tags[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

static const Layout layouts[] = {
    {"DwindleGaps", dwindlegaps}, {"Floating", NULL}, {"Full", monocle},
    {"Dwindle", dwindle},         {"Master", tile},
};

#define MODKEY Mod4Mask

static const Button buttons[] = {
    /* click                event mask      button          function argument */
    {ClkLtSymbol, 0, Button1, setlayout, {0}},
    {ClkLtSymbol, 0, Button3, setlayout, {.v = &layouts[2]}},
    {ClkWinTitle, 0, Button2, zoom, {0}},
    {ClkClientWin, MODKEY, Button1, movemouse, {0}},
    {ClkClientWin, MODKEY, Button2, toggleWindowFloating, {0}},
    {ClkClientWin, MODKEY, Button3, resizemouse, {0}},
    {ClkTagBar, 0, Button1, view, {0}},
    {ClkTagBar, 0, Button3, toggleview, {0}},
    {ClkTagBar, MODKEY, Button1, tag, {0}},
    {ClkTagBar, MODKEY, Button3, toggletag, {0}},
};

void handleMouseButtonPress(XEvent *e) {
  unsigned int i, x, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = findMonitorFromWindow(ev->window)) && m != selectedMonitor) {
    unfocus(selectedMonitor->active, 1);
    selectedMonitor = m;
    focus(NULL);
  }
  if (ev->window == selectedMonitor->dashWin) {
    i = x = 0;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
    } else if (ev->x < x + TEXTW(selectedMonitor->layoutSymbol))
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
    if (c != selectedMonitor->active && !c->isUrgent)
      setWindowUrgent(c, 1);
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
      m = c->monitor;
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

void handleWindowDestroy(XEvent *e) {
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = findClientFromWindow(ev->window)))
    unmanage(c, 1);
}

void handleMouseEnter(XEvent *e) {
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
      ev->window != root)
    return;
  c = findClientFromWindow(ev->window);
  m = c ? c->monitor : findMonitorFromWindow(ev->window);
  if (m != selectedMonitor) {
    unfocus(selectedMonitor->active, 1);
    selectedMonitor = m;
  } else if (!c || c == selectedMonitor->active)
    return;
  focus(c);
}

void handleExpose(XEvent *e) {
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = findMonitorFromWindow(ev->window)))
    drawDash(m);
}

/* there are some broken focus acquiring clients needing extra handling */
void handleFocusIn(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (selectedMonitor->active && ev->window != selectedMonitor->active->win)
    setfocus(selectedMonitor->active);
}

void handleMouseMotion(XEvent *e) {
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = getMonitorForArea(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selectedMonitor->active, 1);
    selectedMonitor = m;
    focus(NULL);
  }
  mon = m;
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
        arrange(c->monitor);
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
      if (c == c->monitor->active)
        drawDash(c->monitor);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updateWindowTypeProps(c);
  }
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

void handleKeymappingChange(XEvent *e) {
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    registerKeyboardShortcuts();
}

void handleWindowMappingRequest(XEvent *e) {
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!findClientFromWindow(ev->window))
    manage(ev->window, &wa);
}

void handleKeypress(XEvent *e) {
  XKeyEvent *ev = &e->xkey;
  KeySym keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  unsigned int cleanMask = CLEANMASK(ev->state);

  for (int i = 0; i < cfg.keybindingCount; i++) {
    if (cfg.keybindings[i].keysym == keysym &&
        CLEANMASK(cfg.keybindings[i].modifier) == cleanMask) {
      executeKeybinding(&cfg.keybindings[i]);
      return;
    }
  }
}

void handleWindowConfigChange(XEvent *e) {
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
        XMoveResizeWindow(dpy, m->dashWin, m->wx, m->dashPos, m->ww, bh);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}
