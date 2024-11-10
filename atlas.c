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
#include "configurer.h"
#include "ipc.h"
#include "util.h"

/* variables */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static void (*handler[LASTEvent])(XEvent *) = {
    [ButtonPress] = handleMouseButtonPress,
    [ClientMessage] = handleClientMessage,
    [ConfigureRequest] = handleConfigureRequest,
    [ConfigureNotify] = handleWindowConfigChange,
    [DestroyNotify] = handleWindowDestroy,
    [EnterNotify] = handleMouseEnter,
    [Expose] = handleExpose,
    [FocusIn] = handleFocusIn,
    [KeyPress] = handleKeypress,
    [MappingNotify] = handleKeymappingChange,
    [MapRequest] = handleWindowMappingRequest,
    [MotionNotify] = handleMouseMotion,
    [PropertyNotify] = handlePropertyChange,
    [UnmapNotify] = handleWindowUnmap};
static int running = 1;

/* external variables */
Drw *drw;
Atom wmatom[WMLast], netatom[NetLast];
Clr **scheme;
Cur *cursor[CurLast];
Display *dpy;
Monitor *monitors, *selectedMonitor;
Window root, wmcheckwin;
int screenWidth, screenHeight; /* X display screen geometry width, height */
int bh;                        /* bar height */
unsigned int numlockmask = 0;
int lrpad; /* sum of left and right padding for text */
char stext[256];
int screen;

/* HACK: Need to implement TOML config for these */
static void reload(const Arg *arg) { reload_config(); }
static const char *fonts[] = {"Inter:size=12"};
static const char col_gray1[] = "#222222";
static const char col_gray2[] = "#444444";
static const char col_gray3[] = "#bbbbbb";
static const char col_gray4[] = "#eeeeee";
static const char col_cyan[] = "#005577";
static const char *colors[][3] = {
    /*               fg         bg         border   */
    [SchemeNorm] = {col_gray3, col_gray1, col_gray2},
    [SchemeSel] = {col_gray4, col_cyan, col_cyan},
};

/* tagging */
static const char *tags[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

static const Layout layouts[] = {
    {"DwindleGaps", dwindlegaps}, {"Floating", NULL}, {"Full", monocle},
    {"Dwindle", dwindle},         {"Master", tile},
};

// Array of startup programs
static const char *exec[] = {"kitty",    NULL,        "picom", NULL,
                             "nitrogen", "--restore", NULL};

#define MODKEY Mod4Mask
#define TAGKEYS(KEY, TAG)                                                      \
  {MODKEY, KEY, view, {.ui = 1 << TAG}},                                       \
      {MODKEY | ControlMask, KEY, toggleview, {.ui = 1 << TAG}},               \
      {MODKEY | ShiftMask, KEY, tag, {.ui = 1 << TAG}},                        \
      {MODKEY | ControlMask | ShiftMask, KEY, toggletag, {.ui = 1 << TAG}},

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
/* HACK: End of hack*/

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
  char limitexceeded[LENGTH(tags) > 31 ? -1 : 1];
};

/* function implementations */

void attach(Client *c) {
  if (!c->monitor->clients) {
    // If there are no clients, this becomes the first one
    c->monitor->clients = c;
    c->next = NULL;
    return;
  }

  // Find the last client
  Client *last;
  for (last = c->monitor->clients; last->next; last = last->next)
    ;

  // Append the new client
  last->next = c;
  c->next = NULL;
}

void attachWindowToStack(Client *c) {
  c->nextInStack = c->monitor->stack;
  c->monitor->stack = c;
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
  XUnmapWindow(dpy, mon->dashWin);
  XDestroyWindow(dpy, mon->dashWin);
  free(mon);
}

Monitor *createMonitor(void) {
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->workspaceset[0] = m->workspaceset[1] = 1;
  m->masterFactor = cfg.masterFactor;
  m->numMasterWindows = cfg.numMasterWindows;
  m->showDash = cfg.showDash;
  m->dashPosTop = cfg.topBar;
  m->layouts[0] = &layouts[0];
  m->layouts[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->layoutSymbol, layouts[0].symbol, sizeof m->layoutSymbol);
  return m;
}

void detach(Client *c) {
  Client **tc;

  for (tc = &c->monitor->clients; *tc && *tc != c; tc = &(*tc)->next)
    ;
  *tc = c->next;
}

void detachWindowFromStack(Client *c) {
  Client **tc, *t;

  for (tc = &c->monitor->stack; *tc && *tc != c; tc = &(*tc)->nextInStack)
    ;
  *tc = c->nextInStack;

  if (c == c->monitor->active) {
    for (t = c->monitor->stack; t && !ISVISIBLE(t); t = t->nextInStack)
      ;
    c->monitor->active = t;
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

void focus(Client *c) {
  if (!c || !ISVISIBLE(c))
    for (c = selectedMonitor->stack; c && !ISVISIBLE(c); c = c->nextInStack)
      ;
  if (selectedMonitor->active && selectedMonitor->active != c)
    unfocus(selectedMonitor->active, 0);
  if (c) {
    if (c->monitor != selectedMonitor)
      selectedMonitor = c->monitor;
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
  selectedMonitor->active = c;
  drawDashes();
}

void focusMonitor(const Arg *arg) {
  Monitor *m;

  if (!monitors->next)
    return;
  if ((m = findMonitorInDirection(arg->i)) == selectedMonitor)
    return;
  unfocus(selectedMonitor->active, 0);
  selectedMonitor = m;
  focus(NULL);
  moveCursorToClientCenter(selectedMonitor->active);
}

void focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selectedMonitor->active ||
      (selectedMonitor->active->isFullscreen && cfg.lockFullscreen))
    return;
  if (arg->i > 0) {
    for (c = selectedMonitor->active->next; c && !ISVISIBLE(c); c = c->next)
      ;
    if (!c)
      for (c = selectedMonitor->clients; c && !ISVISIBLE(c); c = c->next)
        ;
  } else {
    for (i = selectedMonitor->clients; i != selectedMonitor->active;
         i = i->next)
      if (ISVISIBLE(i))
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i))
          c = i;
  }
  if (c) {
    focus(c);
    moveCursorToClientCenter(c);
    restack(selectedMonitor);
  }
}

int getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
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
  // Clear any existing key bindings
  XUngrabKey(dpy, AnyKey, AnyModifier, root);

  // Get numlock mask
  updatenumlockmask();

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

char **parse_command_string(const char *cmd) {
  if (!cmd)
    return NULL;

  // Count the number of arguments needed
  int count = 1; // Start with 1 for the first arg
  const char *tmp = cmd;
  while (*tmp) {
    if (*tmp == ' ')
      count++;
    tmp++;
  }

  // Allocate array of string pointers (plus one for NULL terminator)
  char **argv = calloc(count + 1, sizeof(char *));
  if (!argv)
    return NULL;

  // Make a copy of cmd that we can modify
  char *cmd_copy = strdup(cmd);
  if (!cmd_copy) {
    free(argv);
    return NULL;
  }

  // Parse the arguments
  int i = 0;
  char *token = strtok(cmd_copy, " ");
  while (token && i < count) {
    argv[i] = strdup(token);
    token = strtok(NULL, " ");
    i++;
  }
  argv[i] = NULL; // NULL terminate the array

  free(cmd_copy);
  return argv;
}

// Helper function to free the argument array
void free_command_args(char **argv) {
  if (!argv)
    return;
  for (int i = 0; argv[i] != NULL; i++) {
    free(argv[i]);
  }
  free(argv);
}

// Update the spawn case in execute_keybinding
void executeKeybinding(Keybinding *kb) {
  Arg arg = {0};
  Arg direction = {0};

  switch (kb->action) {
  case ACTION_SPAWN:
    if (kb->value[0]) {
      char **argv = parse_command_string(kb->value);
      if (argv) {
        arg.v = argv;
        spawn(&arg);
        free_command_args(argv);
      } else {
        LOG_ERROR("Failed to parse command: %s", kb->value);
      }
    }
    break;

  case ACTION_KILLCLIENT:
    killclient(&arg);
    break;

  case ACTION_TOGGLEDASH:
    toggleDash(&arg);
    break;

  case ACTION_RELOAD:
    reload(&arg);
    break;

  case ACTION_CYCLEFOCUS:
    focusstack(&arg);
    break;

  case ACTION_FOCUSMONITOR:
    if (!kb->value[0]) {
      LOG_ERROR("No direction specified for focusmonitor keybinding");
      return;
    }
    // New empty variable to hold the direction
    if (strcasecmp(kb->value, "next") == 0) {
      direction.i = +1;
    } else if (strcasecmp(kb->value, "prev") == 0) {
      direction.i = -1;
    } else {
      LOG_ERROR("Invalid direction specified for focusmonitor keybinding, ",
                kb->value);
    }
    focusMonitor(&direction);
    break;

  case ACTION_MOVETOMONITOR:
    if (!kb->value[0]) {
      LOG_ERROR("No direction specified for focusmonitor keybinding");
      return;
    }
    // New empty variable to hold the direction
    if (strcasecmp(kb->value, "next") == 0) {
      direction.i = +1;
    } else if (strcasecmp(kb->value, "prev") == 0) {
      direction.i = -1;
    } else {
      LOG_ERROR("Invalid direction specified for focusmonitor keybinding, ",
                kb->value);
    }
    directWindowToMonitor(&direction);
    break;

  case ACTION_TOGGLEFLOATING:
    toggleWindowFloating(&arg);
    break;

  case ACTION_QUIT:
    quit(&arg);
    break;

  default:
    LOG_WARN("Unknown action for keybinding");
    break;
  }
}

void killclient(const Arg *arg) {
  if (!selectedMonitor->active)
    return;
  if (!sendevent(selectedMonitor->active, wmatom[WMDelete])) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selectedMonitor->active->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

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
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
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

Client *getNextTiledWindow(Client *c) {
  for (; c && (c->isFloating || !ISVISIBLE(c)); c = c->next)
    ;
  return c;
}

void pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->monitor);
}

void moveCursorToClientCenter(Client *c) {
  if (!c || !cfg.moveCursorWithFocus)
    return;

  // Calculate center coordinates of the window
  int x = c->x + (c->w / 2);
  int y = c->y + (c->h / 2);

  // Move cursor to window center
  XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
  XFlush(dpy);
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
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
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

void restack(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawDash(m);
  if (!m->active)
    return;
  if (m->active->isFloating || !m->layouts[m->selectedLayout]->arrange)
    XRaiseWindow(dpy, m->active->win);
  if (m->layouts[m->selectedLayout]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->dashWin;
    for (c = m->stack; c; c = c->nextInStack)
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

void setfocus(Client *c) {
  if (!c->neverFocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&(c->win), 1);
  }
  sendevent(c, wmatom[WMTakeFocus]);
}

// Function to run list of programs at startup
void startupPrograms() {
  pid_t pid;
  const char **args;
  int i;

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

  // Load configuration
  char config_path[256];
  char *home = getenv("HOME");
  if (home) {
    snprintf(config_path, sizeof(config_path), "%s/.config/atlaswm/config.toml",
             home);
    if (load_config(config_path)) {
      LOG_INFO("Configuration loaded successfully");
    } else {
      LOG_WARN("Failed to load config file, using defaults");
    }
  } else {
    LOG_WARN("Could not get HOME directory, using default configuration");
  }

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
  if (selectedMonitor->active && arg->ui & TAGMASK) {
    selectedMonitor->active->workspaces = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void directWindowToMonitor(const Arg *arg) {
  if (!selectedMonitor->active || !monitors->next)
    return;
  sendWindowToMonitor(selectedMonitor->active, findMonitorInDirection(arg->i));
  focusMonitor(arg);
  moveCursorToClientCenter(selectedMonitor->active);
}

void toggletag(const Arg *arg) {
  unsigned int newtags;

  if (!selectedMonitor->active)
    return;
  newtags = selectedMonitor->active->workspaces ^ (arg->ui & TAGMASK);
  if (newtags) {
    selectedMonitor->active->workspaces = newtags;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void toggleview(const Arg *arg) {
  unsigned int newtagset =
      selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] ^
      (arg->ui & TAGMASK);

  if (newtagset) {
    selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] =
        newtagset;
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

void view(const Arg *arg) {
  if ((arg->ui & TAGMASK) ==
      selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces])
    return;
  selectedMonitor->selectedWorkspaces ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK)
    selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] =
        arg->ui & TAGMASK;
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
    if (w == m->dashWin)
      return m;
  if ((c = findClientFromWindow(w)))
    return c->monitor;
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
  Client *c = selectedMonitor->active;

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
