#ifndef _ATLASWM_H_
#define _ATLASWM_H_

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

/* Forward Declarations */
typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct Drw Drw;
typedef struct Fnt Fnt;

/* Enumerations */
// Cursor types
enum {
  CurNormal, // Default cursor
  CurResize, // Cursor when resizing windows
  CurMove,   // Cursor when moving windows
  CurLast    // Marker for last cursor type
};

// Color scheme indices
enum {
  SchemeNorm, // Normal color scheme
  SchemeSel   // Selected color scheme
};

// Color indices within schemes
enum {
  ColFg,    // Foreground color
  ColBg,    // Background color
  ColBorder // Border color
};

// EWMH (Extended Window Manager Hints) atoms
enum {
  NetSupported,
  NetWMName,
  NetWMState,
  NetWMCheck,
  NetWMFullscreen,
  NetActiveWindow,
  NetWMWindowType,
  NetWMWindowTypeDialog,
  NetClientList,
  NetLast
};

// Default X11 atoms
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };

// Click locations for event handling
enum {
  ClkTagBar,     // Tag bar area
  ClkLtSymbol,   // Layout symbol
  ClkStatusText, // Status text area
  ClkWinTitle,   // Window title area
  ClkClientWin,  // Client window
  ClkRootWin,    // Root window
  ClkLast        // Marker for last click type
};

/* Data Structures */

// Generic argument union for flexible parameter passing
typedef union {
  int i;           // Integer value
  unsigned int ui; // Unsigned integer value
  float f;         // Float value
  const void *v;   // Void pointer for generic data
} Arg;

// Mouse button configuration
typedef struct {
  unsigned int click;           // Click type
  unsigned int mask;            // Modifier mask
  unsigned int button;          // Button number
  void (*func)(const Arg *arg); // Function to call
  const Arg arg;                // Argument to pass
} Button;

// Keyboard shortcut configuration
typedef struct {
  unsigned int mod;          // Modifier keys
  KeySym keysym;             // Key symbol
  void (*func)(const Arg *); // Function to call
  const Arg arg;             // Argument to pass
} Key;

// Keybinding configuration

#define MAX_KEYBINDINGS 100
#define MAX_VALUE_LENGTH 256

typedef enum {
  ACTION_SPAWN,
  ACTION_TOGGLEDASH,
  ACTION_RELOAD,
  ACTION_CYCLEFOCUS,
  ACTION_KILLCLIENT,
  ACTION_TOGGLEFLOATING,
  ACTION_FOCUSMONITOR,
  ACTION_MOVETOMONITOR,
  ACTION_QUIT,
  ACTION_UNKNOWN
} ActionType;

typedef struct {
  unsigned int modifier;
  KeySym keysym;
  ActionType action;
  char value[MAX_VALUE_LENGTH];
  char description[MAX_VALUE_LENGTH];
} Keybinding;

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
} Cur;

// Font structure
struct Fnt {
  Display *dpy;       // Display connection
  unsigned int h;     // Font height
  XftFont *xfont;     // Xft font
  FcPattern *pattern; // Font pattern
  struct Fnt *next;   // Next font in chain
};

// Color type definition
typedef XftColor Clr;

// Drawing context structure
struct Drw {
  unsigned int w, h; // Width and height
  Display *dpy;      // Display connection
  int screen;        // Screen number
  Window root;       // Root window
  Drawable drawable; // Drawing surface
  GC gc;             // Graphics context
  Clr *scheme;       // Color scheme
  Fnt *fonts;        // Font set
};

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
  int numMasterWindows;            // Number of windows in master area
  int num;                         // Monitor number
  int dashPos;                     // Bar y position
  int mx, my, mw, mh;              // Monitor geometry
  int wx, wy, ww, wh;              // Window area geometry
  unsigned int selectedWorkspaces; // Current workspace selection
  unsigned int selectedLayout;     // Current layout
  unsigned int workspaceset[2];    // Workspace sets
  int showDash;                    // Bar visibility
  int dashPosTop;                  // Bar position
  Client *clients;                 // List of clients
  Client *active;                  // Selected client
  Client *stack;                   // Client stack
  Monitor *next;                   // Next monitor
  Window dashWin;                  // Bar window
  const Layout *layouts[2];        // Available layouts
};

/* Drawing Functions */
// Drawable creation and management
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w,
                unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

// Font management
Fnt *drw_fontset_create(Drw *drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt *set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text,
                                        unsigned int n);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len,
                      unsigned int *w, unsigned int *h);

// Color management
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
Clr *drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount);

// Cursor management
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

// Drawing context manipulation
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);

// Basic drawing operations
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h,
              int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h,
             unsigned int lpad, const char *text, int invert);
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w,
             unsigned int h);

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
  (mask & ~(numlockmask | LockMask) &                                          \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |      \
    Mod5Mask))
#define INTERSECT(x, y, w, h, m)                                               \
  (MAX(0, MIN((x) + (w), (m)->wx + (m)->ww) - MAX((x), (m)->wx)) *             \
   MAX(0, MIN((y) + (h), (m)->wy + (m)->wh) - MAX((y), (m)->wy)))
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define TAGMASK ((1 << LENGTH(tags)) - 1)
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
void directWindowToMonitor(const Arg *arg);

// Client Functions
void attach(Client *c);
void detach(Client *c);
void attachWindowToStack(Client *c);
void detachWindowFromStack(Client *c);
Client *findClientFromWindow(Window w);
Client *getNextTiledWindow(Client *c);
void updateclientlist(void);

// Event Handling Functions
void handleMouseButtonPress(XEvent *e);
void handleClientMessage(XEvent *e);
void handleConfigureRequest(XEvent *e);
void handleWindowDestroy(XEvent *e);
void handleMouseEnter(XEvent *e);
void handleExpose(XEvent *e);
void handleFocusIn(XEvent *e);
void handleMouseMotion(XEvent *e);
void handlePropertyChange(XEvent *e);
void handleWindowUnmap(XEvent *e);
void handleKeymappingChange(XEvent *e);
void handleWindowMappingRequest(XEvent *e);
void handleKeypress(XEvent *e);
void handleWindowConfigChange(XEvent *e);

// Dashboard Functions
void drawDash(Monitor *m);
void drawDashboards(void);
void updateDashPosition(Monitor *m);
void updateDashboards(void);
void updatestatus(void);
void toggleDash(const Arg *arg);

// Layout Functions
void arrange(Monitor *m);
void arrangeMonitor(Monitor *m);
void setlayout(const Arg *arg);
void setMasterRatio(const Arg *arg);
void incNumMasterWindows(const Arg *arg);
void tile(Monitor *m);
void monocle(Monitor *m);
void dwindle(Monitor *m);
void dwindlegaps(Monitor *m);
void restack(Monitor *m);

// Input Handling Functions
void registerMouseButtons(Client *c, int focused);
void registerKeyboardShortcuts(void);
void updatenumlockmask(void);
void movemouse(const Arg *arg);
void resizemouse(const Arg *arg);
int getrootptr(int *x, int *y);

// Action Functions
void executeKeybinding(Keybinding *kb);
void free_command_args(char **argv);
char **parse_command_string(const char *cmd);
void killclient(const Arg *arg);
void quit(const Arg *arg);
void spawn(const Arg *arg);
void tag(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void view(const Arg *arg);
void zoom(const Arg *arg);
void pop(Client *c);

// Main Functions
void checkForOtherWM(void);
void cleanup(void);
void run(void);
void scan(void);
void setup(void);
int xerror(Display *dpy, XErrorEvent *ee);
int xerrordummy(Display *dpy, XErrorEvent *ee);
int xerrorstart(Display *dpy, XErrorEvent *ee);

/* EWMH Functions */
void ewmh_update_client_list(void);
void ewmh_update_active_window(void);

/* External Variables */
extern Display *dpy;
extern Monitor *monitors;
extern Monitor *selectedMonitor;
extern Drw *drw;
extern Clr **scheme;
extern Cur *cursor[CurLast];
extern Window root;
extern Atom wmatom[WMLast], netatom[NetLast];
extern int bh;
extern unsigned int numlockmask;
extern int screenWidth, screenHeight;
extern int lrpad;
extern char stext[256];
extern int screen;

#endif // _ATLASWM_H_
