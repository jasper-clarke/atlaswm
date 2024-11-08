#include "ipc.h"
#include "configurer.h"
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#define ATLASWM_COMMAND "_ATLASWM_COMMAND"

Atom command_atom = None;

Atom get_command_atom(Display *dpy) {
  return XInternAtom(dpy, ATLASWM_COMMAND, False);
}

void setup_ipc(Display *dpy) {
  command_atom = get_command_atom(dpy);

  Window root = DefaultRootWindow(dpy);
  CommandType initial = 0;
  XChangeProperty(dpy, root, command_atom, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&initial, 1);
}

int send_command(Display *dpy, CommandType cmd) {
  Window root = DefaultRootWindow(dpy);
  Atom command_atom = get_command_atom(dpy);

  // Check if AtlasWM is running by looking for the command atom
  Atom type;
  int format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;

  // Try to read the property just to see if it exists
  if (XGetWindowProperty(dpy, root, command_atom, 0, 1, False, XA_CARDINAL,
                         &type, &format, &nitems, &bytes_after,
                         &data) == Success) {
    if (data)
      XFree(data);

    // Send the command
    CommandType cmd_data = cmd;
    XChangeProperty(dpy, root, command_atom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&cmd_data, 1);
    XFlush(dpy);
    return 1;
  }

  LOG_ERROR("No running instance of AtlasWM found");
  return 0;
}

void handle_command(CommandType cmd) {
  switch (cmd) {
  case CMD_RELOAD:
    LOG_INFO("Received reload command");
    reload_config();
    break;
  default:
    LOG_ERROR("Unknown command received: %d", cmd);
  }
}
