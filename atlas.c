#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
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

#include "atlas.h"
#include "config.h"
#include "ipc.h"
#include "util.h"

/* variables */
void (*eventHandlers[LASTEvent])(XEvent *) = {
    [ButtonPress] = handleMouseButtonPress,
    [ClientMessage] = handleClientMessage,
    [ConfigureRequest] = handleConfigureRequest,
    [ConfigureNotify] = handleWindowConfigChange,
    [DestroyNotify] = handleWindowDestroy,
    [EnterNotify] = handleMouseEnter,
    [FocusIn] = handleFocusIn,
    [KeyPress] = handleKeypress,
    [MappingNotify] = handleKeymappingChange,
    [MapRequest] = handleWindowMappingRequest,
    [MotionNotify] = handleMouseMotion,
    [PropertyNotify] = handlePropertyChange,
    [UnmapNotify] = handleWindowUnmap};

DrawContext *drawContext;
Atom wmAtoms[WM_ATOM_COUNT], netAtoms[NET_ATOM_COUNT];
Window root, wmCheckWindow;
CursorWrapper *cursor[CURSOR_COUNT];
int isWMRunning = 1;

Display *display;
Monitor *monitors, *selectedMonitor;
int screen, screenWidth, screenHeight;
unsigned int numLockMask = 0;

// Main Functions
static int (*defaultXErrorHandler)(Display *, XErrorEvent *);
static void checkForOtherWM(void);
static void cleanupWindowManager(void);
static void runWindowManager(void);
static void scan(void);
static void setupSignalHandlers(void);
static void initAtoms(void);
static void initCursors(void);
static void initWMCheck(void);
static void initWindowManager(void);
int handleXError(Display *dpy, XErrorEvent *ee);
int handleXErrorDummy(Display *dpy, XErrorEvent *ee);
int handleXErrorStart(Display *dpy, XErrorEvent *ee);

void checkForOtherWM(void) {
  defaultXErrorHandler = XSetErrorHandler(handleXErrorStart);
  /* this causes an error if some other window manager is running */
  XSelectInput(display, DefaultRootWindow(display), SubstructureRedirectMask);
  XSync(display, False);
  XSetErrorHandler(handleXError);
  XSync(display, False);
}

void cleanupWindowManager(void) {
  Arg a = {.ui = ~0};
  Layout foo = {"", NULL};
  Monitor *m;
  size_t i;

  viewWorkspace(&a);
  selectedMonitor->layouts[selectedMonitor->selectedLayout] = &foo;
  for (m = monitors; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, 0);
  XUngrabKey(display, AnyKey, AnyModifier, root);
  while (monitors)
    cleanupMonitor(monitors);
  for (i = 0; i < CURSOR_COUNT; i++)
    drw_cur_free(drawContext, cursor[i]);
  XDestroyWindow(display, wmCheckWindow);
  drw_free(drawContext);
  XSync(display, False);
  XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(display, root, netAtoms[NET_ACTIVE_WINDOW]);
}

void runWindowManager(void) {
  XEvent ev;
  XSync(display, False);

  while (isWMRunning && !XNextEvent(display, &ev))
    if (eventHandlers[ev.type])
      eventHandlers[ev.type](&ev); /* call handler */
}

void scan(void) {
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(display, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(display, wins[i], &wa) ||
          wa.override_redirect || XGetTransientForHint(display, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(display, wins[i], &wa))
        continue;
      if (XGetTransientForHint(display, wins[i], &d1) &&
          (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void startupPrograms(void) {
  if (!cfg.startup_progs || cfg.startup_prog_count == 0) {
    return;
  }

  for (int i = 0; i < cfg.startup_prog_count; i++) {
    StartupProgram *prog = &cfg.startup_progs[i];
    if (!prog->command || !prog->args) {
      LOG_ERROR("Invalid startup program at index %d", i);
      continue;
    }

    pid_t pid = fork();
    if (pid == -1) {
      LOG_ERROR("Failed to fork for '%s': %s", prog->command, strerror(errno));
      continue;
    }

    if (pid == 0) { // Child process
      // Close X connection in child
      if (display) {
        close(ConnectionNumber(display));
      }

      // Create new session
      if (setsid() == -1) {
        LOG_ERROR("setsid failed for '%s': %s", prog->command, strerror(errno));
        exit(EXIT_FAILURE);
      }

      // Execute the program
      execvp(prog->command, prog->args);

      // If we get here, execvp failed
      LOG_ERROR("Failed to execute '%s': %s", prog->command, strerror(errno));
      exit(EXIT_FAILURE);
    }

    // Parent process
    LOG_INFO("Started program: %s (pid: %d)", prog->command, pid);
  }
}

void setupSignalHandlers(void) {
  struct sigaction sa;
  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

void initAtoms(void) {
  wmAtoms[WM_PROTOCOLS] = XInternAtom(display, "WM_PROTOCOLS", False);
  wmAtoms[WM_DELETE] = XInternAtom(display, "WM_DELETE_WINDOW", False);
  wmAtoms[WM_STATE] = XInternAtom(display, "WM_STATE", False);
  wmAtoms[WM_TAKE_FOCUS] = XInternAtom(display, "WM_TAKE_FOCUS", False);
  netAtoms[NET_ACTIVE_WINDOW] =
      XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
  netAtoms[NET_SUPPORTED] = XInternAtom(display, "_NET_SUPPORTED", False);
  netAtoms[NET_WM_NAME] = XInternAtom(display, "_NET_WM_NAME", False);
  netAtoms[NET_WM_STATE] = XInternAtom(display, "_NET_WM_STATE", False);
  netAtoms[NET_WM_CHECK] =
      XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
  netAtoms[NET_WM_FULLSCREEN] =
      XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
  netAtoms[NET_WM_WINDOW_TYPE] =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  netAtoms[NET_WM_WINDOW_TYPE_DIALOG] =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netAtoms[NET_CLIENT_LIST] = XInternAtom(display, "_NET_CLIENT_LIST", False);
  netAtoms[NET_DESKTOP_VIEWPORT] =
      XInternAtom(display, "_NET_DESKTOP_VIEWPORT", False);
  netAtoms[NET_NUMBER_OF_DESKTOPS] =
      XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
  netAtoms[NET_CURRENT_DESKTOP] =
      XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
  netAtoms[NET_DESKTOP_NAMES] =
      XInternAtom(display, "_NET_DESKTOP_NAMES", False);
}

void initCursors(void) {
  cursor[CURSOR_NORMAL] = drw_cur_create(drawContext, XC_left_ptr);
  cursor[CURSOR_RESIZE] = drw_cur_create(drawContext, XC_sizing);
  cursor[CURSOR_MOVE] = drw_cur_create(drawContext, XC_fleur);
}

void initWMCheck(void) {
  Atom utf8string;
  Window check = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
  utf8string = XInternAtom(display, "UTF8_STRING", False);
  XChangeProperty(display, check, netAtoms[NET_WM_NAME], utf8string, 8,
                  PropModeReplace, (unsigned char *)"AtlasWM", 7);
  XChangeProperty(display, check, netAtoms[NET_WM_CHECK], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(display, root, netAtoms[NET_WM_CHECK], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&check, 1);
}

void initWindowManager(void) {
  XSetWindowAttributes wa;

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

  setupSignalHandlers();
  screen = DefaultScreen(display);
  screenWidth = DisplayWidth(display, screen);
  screenHeight = DisplayHeight(display, screen);
  root = RootWindow(display, screen);

  drawContext = drw_create(display, screen, root, screenWidth, screenHeight);

  updateMonitorGeometry();
  initAtoms();
  initCursors();
  initWMCheck();
  setup_ipc(display);

  XChangeProperty(display, root, netAtoms[NET_SUPPORTED], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)netAtoms, NET_ATOM_COUNT);

  // Initialize monitor workspaces
  setNumDesktops();
  setCurrentDesktop();
  setDesktopNames();
  setViewport();
  XDeleteProperty(display, root, netAtoms[NET_CLIENT_LIST]);

  // Setup root window event mask
  wa.cursor = cursor[CURSOR_NORMAL]->cursor;
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                  ButtonPressMask | PointerMotionMask | EnterWindowMask |
                  LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
  XChangeWindowAttributes(display, root, CWEventMask | CWCursor, &wa);
  XSelectInput(display, root, wa.event_mask);

  // Register keyboard shortcuts
  registerKeyboardShortcuts();
  Monitor *m;
  for (m = monitors; m; m = m->next) {
    m->workspaceset[0] = m->workspaceset[1] = 1;
  }
  focus(NULL);
  startupPrograms();
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int handleXError(Display *dpy, XErrorEvent *ee) {
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

  char error_text[1024];
  XGetErrorText(dpy, ee->error_code, error_text, sizeof(error_text));

  LOG_ERROR("X Error: request=%d error=%d (%s) resourceid=%lu serial=%lu",
            ee->request_code, ee->error_code, error_text, ee->resourceid,
            ee->serial);

  return defaultXErrorHandler(dpy, ee);
}

int handleXErrorDummy(Display *dpy, XErrorEvent *ee) { return 0; }

int handleXErrorStart(Display *dpy, XErrorEvent *ee) {
  LOG_FATAL("Another window manager is already running");
  return -1;
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
  if (!(display = XOpenDisplay(NULL)))
    LOG_FATAL("Failed to open display");
  checkForOtherWM();
  LOG_INFO("AtlasWM starting");
  initWindowManager();
  LOG_INFO("AtlasWM setup complete");
  scan();
  runWindowManager();
  LOG_INFO("AtlasWM is exiting");
  cleanupWindowManager();
  XCloseDisplay(display);
  return EXIT_SUCCESS;
}
