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
 * on each monitor. Each client contains a bit array to indicate the workspaces
 * of a client.
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
void (*handler[LASTEvent])(XEvent *) = {
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

/* external variables */
Drw *drw;
Atom wmatom[WMLast], netatom[NetLast];
Cur *cursor[CurLast];
Display *dpy;
Monitor *monitors, *selectedMonitor;
Window root, wmcheckwin;
int screenWidth, screenHeight; /* X display screen geometry width, height */
unsigned int numlockmask = 0;
int screen;
int running = 1;

/* function implementations */
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
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

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

void startupPrograms() {
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
      if (dpy) {
        close(ConnectionNumber(dpy));
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

void setup(void) {
  XSetWindowAttributes wa;
  struct sigaction sa;
  Atom utf8string;

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
  updateMonitorGeometry();
  /* init atoms */
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
  netatom[NetDesktopViewport] =
      XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
  netatom[NetNumberOfDesktops] =
      XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
  netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
  netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] = drw_cur_create(drw, XC_sizing);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  setup_ipc(dpy);
  Window check = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  XChangeProperty(dpy, check, netatom[NetWMName], utf8string, 8,
                  PropModeReplace, (unsigned char *)"AtlasWM", 7);
  XChangeProperty(dpy, check, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&check, 1);

  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)netatom, NetLast);
  setNumDesktops();
  setCurrentDesktop();
  setDesktopNames();
  setViewport();
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                  ButtonPressMask | PointerMotionMask | EnterWindowMask |
                  LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  registerKeyboardShortcuts();
  if (cfg.workspaceCount == 0 || cfg.workspaceCount > 31) {
    LOG_ERROR("Invalid workspace count: %zu. Must be between 1 and 31.",
              cfg.workspaceCount);
    cfg.workspaceCount = 9; // Fallback to default
    cfg.workspaces = ecalloc(cfg.workspaceCount, sizeof(Workspace));
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      char num[2];
      snprintf(num, sizeof(num), "%zu", i + 1);
      cfg.workspaces[i].name = strdup(num);
    }
  }

  // Initialize monitor workspaces
  Monitor *m;
  for (m = monitors; m; m = m->next) {
    // Set initial workspace (traditionally the first one)
    m->workspaceset[0] = m->workspaceset[1] = 1;
  }
  focus(NULL);
  startupPrograms();
}

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

  char error_text[1024];
  XGetErrorText(dpy, ee->error_code, error_text, sizeof(error_text));

  LOG_ERROR("X Error: request=%d error=%d (%s) resourceid=%lu serial=%lu",
            ee->request_code, ee->error_code, error_text, ee->resourceid,
            ee->serial);

  return xerrorxlib(dpy, ee);
}

int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }

int xerrorstart(Display *dpy, XErrorEvent *ee) {
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
