#include "atlas.h"
#include "configurer.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>

/* HACK: Need to implement TOML config for these */
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

void registerMouseButtons(Client *c, int focused) {
  updateNumlockMask();
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
  // Clear any existing key bindings
  XUngrabKey(dpy, AnyKey, AnyModifier, root);

  // Get numlock mask
  updateNumlockMask();

  // Common modifier combinations to handle
  unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};

  // Register all configured keybindings from TOML config
  for (int i = 0; i < cfg.keybindingCount; i++) {
    KeyCode code = XKeysymToKeycode(dpy, cfg.keybindings[i].keysym);
    if (code) {
      // Register the keybinding with all modifier combinations
      for (size_t j = 0; j < LENGTH(modifiers); j++) {
        XGrabKey(dpy, code, cfg.keybindings[i].modifier | modifiers[j], root,
                 True, GrabModeAsync, GrabModeAsync);
      }
    } else {
      LOG_ERROR("Failed to get keycode for keysym in binding %d", i);
    }
  }
}

void updateNumlockMask(void) {
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

// TODO: Rename
void movemouse(const Arg *arg) {
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selectedMonitor->active))
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
      if ((ev.xmotion.time - lasttime) <= (1000 / cfg.refreshRate))
        continue;
      lasttime = ev.xmotion.time;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);
      if (abs(selectedMonitor->wx - nx) < cfg.snapDistance)
        nx = selectedMonitor->wx;
      else if (abs((selectedMonitor->wx + selectedMonitor->ww) -
                   (nx + WIDTH(c))) < cfg.snapDistance)
        nx = selectedMonitor->wx + selectedMonitor->ww - WIDTH(c);
      if (abs(selectedMonitor->wy - ny) < cfg.snapDistance)
        ny = selectedMonitor->wy;
      else if (abs((selectedMonitor->wy + selectedMonitor->wh) -
                   (ny + HEIGHT(c))) < cfg.snapDistance)
        ny = selectedMonitor->wy + selectedMonitor->wh - HEIGHT(c);
      if (!c->isFloating &&
          selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange &&
          (abs(nx - c->x) > cfg.snapDistance ||
           abs(ny - c->y) > cfg.snapDistance))
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

// TODO: Rename
void resizemouse(const Arg *arg) {
  int ocx, ocy, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;
  int isDwindle;

  if (!(c = selectedMonitor->active))
    return;
  if (c->isFullscreen) /* no support resizing fullscreen windows by mouse */
    return;

  isDwindle =
      (selectedMonitor->layouts[selectedMonitor->selectedLayout]->arrange ==
       dwindlegaps);

  restack(selectedMonitor);
  ocx = c->x;
  ocy = c->y;

  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;

  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->borderWidth - 1,
               c->h + c->borderWidth - 1);

  int startW = c->w;
  int startH = c->h;
  float startHRatio = c->horizontalRatio > 0 ? c->horizontalRatio : 0.5;
  float startVRatio = c->verticalRatio > 0 ? c->verticalRatio : 0.5;

  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / cfg.refreshRate))
        continue;
      lasttime = ev.xmotion.time;

      nw = MAX(ev.xmotion.x - ocx - 2 * c->borderWidth + 1, 1);
      nh = MAX(ev.xmotion.y - ocy - 2 * c->borderWidth + 1, 1);

      if (isDwindle) {
        if (c->isFloating) {
          resize(c, c->x, c->y, nw, nh, 1);
        } else {

          // Calculate new ratios based on mouse movement
          float dx = (float)(nw - startW) / startW;
          float dy = (float)(nh - startH) / startH;

          // Update the ratios (bounded between 0.1 and 0.9)
          c->horizontalRatio = CLAMP(startHRatio + (dx / 2), 0.1, 0.9);
          c->verticalRatio = CLAMP(startVRatio + (dy / 2), 0.1, 0.9);

          arrange(selectedMonitor);
        }
      } else {
        // Original floating window resize behavior
        if (!c->isFloating &&
            selectedMonitor->layouts[selectedMonitor->selectedLayout]
                ->arrange &&
            (abs(nw - c->w) > cfg.snapDistance ||
             abs(nh - c->h) > cfg.snapDistance))
          toggleWindowFloating(NULL);

        if (!selectedMonitor->layouts[selectedMonitor->selectedLayout]
                 ->arrange ||
            c->isFloating)
          resize(c, c->x, c->y, nw, nh, 1);
      }
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

// TODO: Rename
int getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}
