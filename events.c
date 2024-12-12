// EVENTS
// "How should this xserver event be handled?"

#include "atlas.h"
#include "config.h"
#include "ipc.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

/* HACK: Need to implement TOML config for these */
#define MODKEY Mod4Mask

static const Button buttons[] = {
    {CLICK_CLIENT_WINDOW, MODKEY, Button1, moveWindow, {0}},
    {CLICK_CLIENT_WINDOW, MODKEY, Button2, toggleWindowFloating, {0}},
    {CLICK_CLIENT_WINDOW, MODKEY, Button3, resizeWindow, {0}},
};

void handleMouseButtonPress(XEvent *e) {
  unsigned int i, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = CLICK_ROOT_WINDOW;
  /* focus monitor if necessary */
  if ((m = findMonitorFromWindow(ev->window)) && m != selectedMonitor) {
    unfocus(selectedMonitor->active, 1);
    selectedMonitor = m;
    focus(NULL);
  }
  if ((c = findClientFromWindow(ev->window))) {
    focus(c);
    restack(selectedMonitor);
    XAllowEvents(display, ReplayPointer, CurrentTime);
    click = CLICK_CLIENT_WINDOW;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func &&
        buttons[i].button == ev->button &&
        CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void handleClientMessage(XEvent *e) {
  XClientMessageEvent *cme = &e->xclient;
  Client *c = findClientFromWindow(cme->window);

  if (!c)
    return;
  if (cme->message_type == netAtoms[NET_WM_STATE]) {
    if (cme->data.l[1] == netAtoms[NET_WM_FULLSCREEN] ||
        cme->data.l[2] == netAtoms[NET_WM_FULLSCREEN])
      setWindowFullscreen(c,
                          (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                           || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                               !c->isFullscreen)));
  } else if (cme->message_type == netAtoms[NET_ACTIVE_WINDOW]) {
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
        XMoveResizeWindow(display, c->win, c->x, c->y, c->w, c->h);
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
    XConfigureWindow(display, ev->window, ev->value_mask, &wc);
  }
  XSync(display, False);
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

    if (XGetWindowProperty(display, root, command_atom, 0, 1, True, XA_CARDINAL,
                           &type, &format, &nitems, &bytes_after,
                           &data) == Success) {
      if (data) {
        CommandType cmd = *(CommandType *)data;
        handle_command(cmd);
        XFree(data);
      }
    }
  } else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = findClientFromWindow(ev->window))) {
    switch (ev->atom) {
    default:
      break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isFloating && (XGetTransientForHint(display, c->win, &trans)) &&
          (c->isFloating = (findClientFromWindow(trans)) != NULL))
        arrange(c->monitor);
      break;
    case XA_WM_NORMAL_HINTS:
      c->hintsvalid = 0;
      break;
    case XA_WM_HINTS:
      updateWindowManagerHints(c);
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netAtoms[NET_WM_NAME]) {
      updateWindowTitle(c);
    }
    if (ev->atom == netAtoms[NET_WM_WINDOW_TYPE])
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

  if (!XGetWindowAttributes(display, ev->window, &wa) || wa.override_redirect)
    return;
  if (!findClientFromWindow(ev->window))
    manage(ev->window, &wa);
}

void handleKeypress(XEvent *e) {
  XKeyEvent *ev = &e->xkey;
  KeySym keysym = XKeycodeToKeysym(display, (KeyCode)ev->keycode, 0);
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
  XConfigureEvent *ev = &e->xconfigure;

  // Only handle root window configuration changes
  if (ev->window != root) {
    return;
  }

  // Check if screen dimensions have changed
  int dimensionsChanged =
      (screenWidth != ev->width || screenHeight != ev->height);
  if (!dimensionsChanged) {
    return;
  }

  // Update screen dimensions
  screenWidth = ev->width;
  screenHeight = ev->height;

  // Update monitor geometry and check if any monitors were affected
  if (!updateMonitorGeometry() && !dimensionsChanged) {
    return; // No changes needed
  }

  // Update all monitors and their clients
  for (Monitor *m = monitors; m; m = m->next) {
    // Resize fullscreen windows to match their monitor
    for (Client *c = m->clients; c; c = c->next) {
      if (c->isFullscreen) {
        resizeclient(c, m->mx, m->my, m->mw, m->mh);
      }
    }
  }

  // Reset focus and rearrange all windows
  focus(NULL);
  arrange(NULL);
}
