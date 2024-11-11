#include "atlas.h"
#include "configurer.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void reload(const Arg *arg) { reload_config(); }

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

void quit(const Arg *arg) { running = 0; }

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

void moveToWorkspace(const Arg *arg) {
  if (selectedMonitor->active && arg->ui & WORKSPACEMASK) {
    selectedMonitor->active->workspaces = arg->ui & WORKSPACEMASK;
    focus(NULL);
    arrange(selectedMonitor);
  }
}

void duplicateToWorkspace(const Arg *arg) {
  unsigned int newtags;

  if (!selectedMonitor->active)
    return;
  newtags = selectedMonitor->active->workspaces ^ (arg->ui & WORKSPACEMASK);
  if (newtags) {
    selectedMonitor->active->workspaces = newtags;
    focus(NULL);
    arrange(selectedMonitor);
  }
  updateCurrentDesktop();
}

void toggleWorkspace(const Arg *arg) {
  unsigned int newtagset =
      selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] ^
      (arg->ui & WORKSPACEMASK);

  if (newtagset) {
    selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] =
        newtagset;
    focus(NULL);
    arrange(selectedMonitor);
  }
  updateCurrentDesktop();
}

void viewWorkspace(const Arg *arg) {
  if ((arg->ui & WORKSPACEMASK) ==
      selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces])
    return;
  selectedMonitor->selectedWorkspaces ^= 1; /* toggle sel tagset */
  if (arg->ui & WORKSPACEMASK)
    selectedMonitor->workspaceset[selectedMonitor->selectedWorkspaces] =
        arg->ui & WORKSPACEMASK;
  focus(NULL);
  arrange(selectedMonitor);
  updateCurrentDesktop();
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

void pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->monitor);
}

void directWindowToMonitor(const Arg *arg) {
  if (!selectedMonitor->active || !monitors->next)
    return;
  sendWindowToMonitor(selectedMonitor->active, findMonitorInDirection(arg->i));
  focusMonitor(arg);
  moveCursorToClientCenter(selectedMonitor->active);
}
