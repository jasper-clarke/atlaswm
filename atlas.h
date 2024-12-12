#ifndef _ATLASWM_H_
#define _ATLASWM_H_

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

/* Forward Declarations */
typedef struct Monitor Monitor;
typedef struct Client Client;

// Constants and Enums
enum CursorType { CURSOR_NORMAL, CURSOR_RESIZE, CURSOR_MOVE, CURSOR_COUNT };

enum NetAtom {
  NET_SUPPORTED,
  NET_WM_NAME,
  NET_WM_STATE,
  NET_WM_CHECK,
  NET_WM_FULLSCREEN,
  NET_ACTIVE_WINDOW,
  NET_WM_WINDOW_TYPE,
  NET_WM_WINDOW_TYPE_DIALOG,
  NET_CLIENT_LIST,
  NET_DESKTOP_NAMES,
  NET_DESKTOP_VIEWPORT,
  NET_NUMBER_OF_DESKTOPS,
  NET_CURRENT_DESKTOP,
  NET_ATOM_COUNT
};

enum WMAtom { WM_PROTOCOLS, WM_DELETE, WM_STATE, WM_TAKE_FOCUS, WM_ATOM_COUNT };

enum ClickTarget { CLICK_CLIENT_WINDOW, CLICK_ROOT_WINDOW, CLICK_COUNT };

typedef enum {
  ACTION_SPAWN,
  ACTION_RELOAD,
  ACTION_CYCLEFOCUS,
  ACTION_KILLCLIENT,
  ACTION_TOGGLEFLOATING,
  ACTION_FOCUSMONITOR,
  ACTION_MOVETOMONITOR,
  ACTION_VIEWWORKSPACE,
  ACTION_MOVETOWORKSPACE,
  ACTION_DUPLICATETOWORKSPACE,
  ACTION_TOGGLEWORKSPACE,
  ACTION_QUIT,
  ACTION_UNKNOWN
} ActionType;

/* Data Structures */
typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct {
  unsigned int click;
  unsigned int mask;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct {
  unsigned int mod;          // Modifier keys
  KeySym keysym;             // Key symbol
  void (*func)(const Arg *); // Function to call
  const Arg arg;             // Argument to pass
} Key;

typedef struct {
  unsigned int modifier;
  KeySym keysym;
  ActionType action;
  char *value;
  char *description;
} Keybinding;

typedef struct {
  char *command; // The main command
  char **args;   // Array of arguments (NULL terminated)
  int arg_count; // Number of arguments
} StartupProgram;

typedef struct {
  char *name; // Workspace name
} Workspace;

// Window rule configuration
typedef struct {
  const char *class;    // Window class
  const char *instance; // Window instance
  const char *title;    // Window title
  unsigned int tags;    // Tag mask
  int isfloating;       // Floating state
  int monitor;          // Monitor number
} Rule;

// Cursor structure
typedef struct {
  Cursor cursor; // X11 cursor
} CursorWrapper;

// Color type definition
typedef XftColor Clr;

// Drawing context structure
typedef struct {
  unsigned int w, h; // Width and height
  Display *dpy;      // Display connection
  int screen;        // Screen number
  Window root;       // Root window
  Drawable drawable; // Drawing surface
  GC gc;             // Graphics context
} DrawContext;

// Client (window) structure
struct Client {
  char name[256];                       // Window title
  float minAspectRatio, maxAspectRatio; // Window aspect ratio constraints
  int x, y, w, h;                       // Current geometry
  int oldx, oldy, oldw, oldh;           // Previous geometry
  float horizontalRatio, verticalRatio; // Position ratios
  int basew, baseh;                     // Minimum size
  int incw, inch;                       // Increment size
  int maxw, maxh, minw, minh;           // Size constraints
  int hintsvalid;                       // Whether size hints are valid
  int borderWidth, oldBorderWidth;      // Border widths
  unsigned int workspaces;              // Tags (virtual desktops)
  int isFixedSize;                      // Whether size is fixed
  int isFloating;                       // Whether window is floating
  int isUrgent;                         // Whether window needs attention
  int neverFocus;                       // Whether window should never get focus
  int previousState;                    // Previous state
  int isFullscreen;                     // Whether window is fullscreen
  Client *next;                         // Next client in list
  Client *nextInStack;                  // Next client in stack
  Monitor *monitor;                     // Monitor containing this client
  Window win;                           // X11 window ID
};

// Layout structure
typedef struct {
  const char *symbol;         // Layout symbol
  void (*arrange)(Monitor *); // Layout arrangement function
} Layout;

// Monitor structure
struct Monitor {
  char layoutSymbol[16];           // Current layout symbol
  float masterFactor;              // Size of master area
  int num;                         // Monitor number
  int mx, my, mw, mh;              // Monitor geometry
  int wx, wy, ww, wh;              // Window area geometry
  unsigned int selectedWorkspaces; // Current workspace selection
  unsigned int selectedLayout;     // Current layout
  unsigned int workspaceset[2];    // Workspace sets
  Client *clients;                 // List of clients
  Client *active;                  // Selected client
  Client *stack;                   // Client stack
  Monitor *next;                   // Next monitor
  const Layout *layouts[2];        // Available layouts
};

/* Drawing Functions */
DrawContext *drw_create(Display *dpy, int screen, Window win, unsigned int w,
                        unsigned int h);
void drw_free(DrawContext *drw);
void drw_clr_create(DrawContext *drw, Clr *dest, const char *clrname);
CursorWrapper *drw_cur_create(DrawContext *drw, int shape);
void drw_cur_free(DrawContext *drw, CursorWrapper *cursor);

/* Utility Macros */
#define HEIGHT(X) ((X)->h + 2 * (X)->borderWidth)
#define WIDTH(X) ((X)->w + 2 * (X)->borderWidth)
#define ISVISIBLE(C)                                                           \
  ((C->workspaces & C->monitor->workspaceset[C->monitor->selectedWorkspaces]))
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))
#define LENGTH(X) (sizeof(X) / sizeof(X[0]))
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)                                                        \
  (mask & ~(numLockMask | LockMask) &                                          \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |      \
    Mod5Mask))
#define INTERSECT(x, y, w, h, m)                                               \
  (MAX(0, MIN((x) + (w), (m)->wx + (m)->ww) - MAX((x), (m)->wx)) *             \
   MAX(0, MIN((y) + (h), (m)->wy + (m)->wh) - MAX((y), (m)->wy)))
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define WORKSPACEMASK ((1U << cfg.workspaceCount) - 1)
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)
#define CLAMP(x, min, max)                                                     \
  (((x) < (min)) ? (min) : (((x) > (max)) ? (max) : (x)))

/* Function Declarations */
// Window Functions
void manage(Window w, XWindowAttributes *wa);
void unmanage(Client *c, int destroyed);
void updateWindowTitle(Client *c);
void updateWindowTypeProps(Client *c);
void updateWindowManagerHints(Client *c);
void updateWindowSizeHints(Client *c);
void configure(Client *c);
void applyWindowRules(Client *c);
int applyWindowSizeConstraints(Client *c, int *x, int *y, int *w, int *h,
                               int interact);
void setWindowFullscreen(Client *c, int fullscreen);
void setWindowUrgent(Client *c, int urg);
void toggleWindowFloating(const Arg *arg);
void toggleWindowVisibility(Client *c);
void resize(Client *c, int x, int y, int w, int h, int interact);
void resizeclient(Client *c, int x, int y, int w, int h);
void setclientstate(Client *c, long state);
int sendevent(Client *c, Atom proto);
int shouldscale(Client *c);
void scaleclient(Client *c, int x, int y, int w, int h, float scale);
Atom getatomprop(Client *c, Atom prop);
long getstate(Window w);
int gettextprop(Window w, Atom atom, char *text, unsigned int size);

// Focus Functions
void focus(Client *c);
void unfocus(Client *c, int setfocus);
void focusstack(const Arg *arg);
void focusMonitor(const Arg *arg);
void setfocus(Client *c);
void moveCursorToClientCenter(Client *c);

// Monitor Functions
Monitor *createMonitor(void);
void cleanupMonitor(Monitor *mon);
int updateMonitorGeometry(void);
Monitor *findMonitorFromWindow(Window w);
Monitor *findMonitorInDirection(int dir);
Monitor *getMonitorForArea(int x, int y, int w, int h);
void sendWindowToMonitor(Client *c, Monitor *m);

// Client Functions
void attach(Client *c);
void detach(Client *c);
void attachWindowToStack(Client *c);
void detachWindowFromStack(Client *c);
Client *findClientFromWindow(Window w);
Client *getNextTiledWindow(Client *c);
void updateClientList(void);

// Event Handling Functions
void handleMouseButtonPress(XEvent *e);
void handleClientMessage(XEvent *e);
void handleConfigureRequest(XEvent *e);
void handleWindowDestroy(XEvent *e);
void handleMouseEnter(XEvent *e);
void handleFocusIn(XEvent *e);
void handleMouseMotion(XEvent *e);
void handlePropertyChange(XEvent *e);
void handleWindowUnmap(XEvent *e);
void handleKeymappingChange(XEvent *e);
void handleWindowMappingRequest(XEvent *e);
void handleKeypress(XEvent *e);
void handleWindowConfigChange(XEvent *e);

// Layout Functions
void arrange(Monitor *m);
void arrangeMonitor(Monitor *m);
void setlayout(const Arg *arg);
void setMasterRatio(const Arg *arg);
void monocle(Monitor *m);
void dwindlegaps(Monitor *m);
void restack(Monitor *m);

// Input Handling Functions
void registerMouseButtons(Client *c, int focused);
void registerKeyboardShortcuts(void);
void updateNumlockMask(void);
void moveWindow(const Arg *arg);
void resizeWindow(const Arg *arg);
int getRootPointer(int *x, int *y);

// Action Functions
void executeKeybinding(Keybinding *kb);
void killclient(const Arg *arg);
void quit(const Arg *arg);
void spawn(const Arg *arg);
void moveToWorkspace(const Arg *arg);
void duplicateToWorkspace(const Arg *arg);
void toggleWorkspace(const Arg *arg);
void viewWorkspace(const Arg *arg);
void zoom(const Arg *arg);
void pop(Client *c);
void directWindowToMonitor(const Arg *arg);
void free_command_args(char **argv);
char **parse_command_string(const char *cmd);

int handleXError(Display *dpy, XErrorEvent *ee);
int handleXErrorDummy(Display *dpy, XErrorEvent *ee);
int handleXErrorStart(Display *dpy, XErrorEvent *ee);

void setCurrentDesktop(void);
void setDesktopNames(void);
void setNumDesktops(void);
void setViewport(void);
void updateCurrentDesktop(void);

/* External Variables */
extern Display *display;
extern Monitor *monitors;
extern Monitor *selectedMonitor;
extern DrawContext *drawContext;
extern CursorWrapper *cursor[CURSOR_COUNT];
extern Window root;
extern Atom wmAtoms[WM_ATOM_COUNT], netAtoms[NET_ATOM_COUNT];
extern unsigned int numLockMask;
extern int screenWidth, screenHeight;
extern int screen;
extern const Layout layouts[];
extern void (*eventHandlers[LASTEvent])(XEvent *);
extern int isWMRunning;

#endif // _ATLASWM_H_
