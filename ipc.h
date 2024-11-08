#ifndef _IPC_H_
#define _IPC_H_

#include <X11/Xlib.h>

// Command types
typedef enum { CMD_RELOAD = 1 } CommandType;

extern Atom command_atom;

// Function declarations
int send_command(Display *dpy, CommandType cmd);
void handle_command(CommandType cmd);
void setup_ipc(Display *dpy);
Atom get_command_atom(Display *dpy);

#endif // _IPC_H_
