#include "atlas.h"
#include "config.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void reload(const Arg *arg) { reload_config(); }

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

    if (strcasecmp(kb->value, "up") == 0) {
      direction.i = DIR_UP;
    } else if (strcasecmp(kb->value, "down") == 0) {
      direction.i = DIR_DOWN;
    } else if (strcasecmp(kb->value, "left") == 0) {
      direction.i = DIR_LEFT;
    } else if (strcasecmp(kb->value, "right") == 0) {
      direction.i = DIR_RIGHT;
    } else {
      LOG_ERROR("Invalid direction specified for focusmonitor keybinding: %s",
                kb->value);
      return;
    }
    focusMonitor(&direction);
    break;

  case ACTION_MOVETOMONITOR:
    if (!kb->value[0]) {
      LOG_ERROR("No direction specified for movetomonitor keybinding");
      return;
    }

    if (strcasecmp(kb->value, "up") == 0) {
      direction.i = DIR_UP;
    } else if (strcasecmp(kb->value, "down") == 0) {
      direction.i = DIR_DOWN;
    } else if (strcasecmp(kb->value, "left") == 0) {
      direction.i = DIR_LEFT;
    } else if (strcasecmp(kb->value, "right") == 0) {
      direction.i = DIR_RIGHT;
    } else {
      LOG_ERROR("Invalid direction specified for movetomonitor keybinding: %s",
                kb->value);
      return;
    }
    directWindowToMonitor(&direction);
    break;

  case ACTION_TOGGLEFLOATING:
    toggleWindowFloating(&arg);
    break;

  case ACTION_VIEWWORKSPACE:
    // Convert workspace name to index
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      if (strcasecmp(kb->value, cfg.workspaces[i].name) == 0) {
        arg.ui = 1 << i;
        break;
      }
    }
    viewWorkspace(&arg);
    break;

  case ACTION_MOVETOWORKSPACE:
    // Convert workspace name to index
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      if (strcasecmp(kb->value, cfg.workspaces[i].name) == 0) {
        arg.ui = 1 << i;
        break;
      }
    }
    moveToWorkspace(&arg);
    break;

  case ACTION_DUPLICATETOWORKSPACE:
    // Convert workspace name to index
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      if (strcasecmp(kb->value, cfg.workspaces[i].name) == 0) {
        arg.ui = 1 << i;
        break;
      }
    }
    duplicateToWorkspace(&arg);
    break;

  case ACTION_TOGGLEWORKSPACE:
    // Convert workspace name to index
    for (size_t i = 0; i < cfg.workspaceCount; i++) {
      if (strcasecmp(kb->value, cfg.workspaces[i].name) == 0) {
        arg.ui = 1 << i;
        break;
      }
    }
    toggleWorkspace(&arg);
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
  if (!sendevent(selectedMonitor->active, wmAtoms[WM_DELETE])) {
    XGrabServer(display);
    XSetErrorHandler(handleXErrorDummy);
    XSetCloseDownMode(display, DestroyAll);
    XKillClient(display, selectedMonitor->active->win);
    XSync(display, False);
    XSetErrorHandler(handleXError);
    XUngrabServer(display);
  }
}

void quit(const Arg *arg) { isWMRunning = 0; }

void spawn(const Arg *arg) {
  struct sigaction sa;

  if (fork() == 0) {
    if (display)
      close(ConnectionNumber(display));
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
