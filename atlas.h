#ifndef _ATLASWM_H_
#define _ATLASWM_H_

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

/* forward declarations */
typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct Drw Drw;
typedef struct Fnt Fnt;

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel };                  /* color schemes */
enum { ColFg, ColBg, ColBorder };
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
}; /* EWMH atoms */
enum {
  WMProtocols,
  WMDelete,
  WMState,
  WMTakeFocus,
  WMLast
}; /* default atoms */
enum {
  ClkTagBar,
  ClkLtSymbol,
  ClkStatusText,
  ClkWinTitle,
  ClkClientWin,
  ClkRootWin,
  ClkLast
}; /* clicks */

/* structs */
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
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  const char *class;
  const char *instance;
  const char *title;
  unsigned int tags;
  int isfloating;
  int monitor;
} Rule;

typedef struct {
  Cursor cursor;
} Cur;

struct Fnt {
  Display *dpy;
  unsigned int h;
  XftFont *xfont;
  FcPattern *pattern;
  struct Fnt *next;
};

typedef XftColor Clr;

struct Drw {
  unsigned int w, h;
  Display *dpy;
  int screen;
  Window root;
  Drawable drawable;
  GC gc;
  Clr *scheme;
  Fnt *fonts;
};

struct Client {
  char name[256];
  float minAspectRatio, maxAspectRatio;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  float horizontalRatio, verticalRatio;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
  int borderWidth, oldBorderWidth;
  unsigned int tags;
  int isFixedSize, isFloating, isUrgent, neverFocus, previousState,
      isFullscreen;
  Client *next;
  Client *snext;
  Monitor *mon;
  Window win;
};

typedef struct {
  const char *symbol;
  void (*arrange)(Monitor *);
} Layout;

struct Monitor {
  char ltsymbol[16];
  float masterFactor;
  int numMasterWindows;
  int num;
  int by;             /* bar geometry */
  int mx, my, mw, mh; /* screen size */
  int wx, wy, ww, wh; /* window area  */
  unsigned int selectedTags;
  unsigned int selectedLayout;
  unsigned int tagset[2];
  int showbar;
  int topbar;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
  const Layout *layouts[2];
};

/* Drawing Functions */
/* Drawable abstraction */
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w,
                unsigned int h);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw *drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt *set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text,
                                        unsigned int n);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len,
                      unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname);
Clr *drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount);

/* Cursor abstraction */
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h,
              int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h,
             unsigned int lpad, const char *text, int invert);

/* Map functions */
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w,
             unsigned int h);

/* Macros */
#define HEIGHT(X) ((X)->h + 2 * (X)->borderWidth)
#define WIDTH(X) ((X)->w + 2 * (X)->borderWidth)
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->selectedTags]))
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))
#define LENGTH(X) (sizeof(X) / sizeof(X[0]))

/* function declarations */
void applyWindowRules(Client *c);
int applyWindowSizeConstraints(Client *c, int *x, int *y, int *w, int *h,
                               int interact);
void arrange(Monitor *m);
void arrangeMonitor(Monitor *m);
void attach(Client *c);
void attachWindowToStack(Client *c);
void handleMouseButtonPress(XEvent *e);
void checkForOtherWM(void);
void cleanup(void);
void cleanupMonitor(Monitor *mon);
void handleClientMessage(XEvent *e);
void configure(Client *c);
void configurenotify(XEvent *e);
void handleConfigureRequest(XEvent *e);
Monitor *createMonitor(void);
void handleWindowDestroy(XEvent *e);
void detach(Client *c);
void detachWindowFromStack(Client *c);
Monitor *findMonitorInDirection(int dir);
void drawDash(Monitor *m);
void drawDashes(void);
void handleMouseEnter(XEvent *e);
void handleExpose(XEvent *e);
void focus(Client *c);
void handleFocusIn(XEvent *e);
void focusMonitor(const Arg *arg);
void focusstack(const Arg *arg);
Atom getatomprop(Client *c, Atom prop);
int getrootptr(int *x, int *y);
long getstate(Window w);
int gettextprop(Window w, Atom atom, char *text, unsigned int size);
Client *getNextTiledWindow(Client *c);
void registerMouseButtons(Client *c, int focused);
void registerKeyboardShortcuts(void);
void incNumMasterWindows(const Arg *arg);
void keypress(XEvent *e);
void killclient(const Arg *arg);
void manage(Window w, XWindowAttributes *wa);
void mappingnotify(XEvent *e);
void maprequest(XEvent *e);
void handleMouseMotion(XEvent *e);
void movemouse(const Arg *arg);
void moveCursorToClientCenter(Client *c);
void pop(Client *c);
void handlePropertyChange(XEvent *e);
void quit(const Arg *arg);
Monitor *getMonitorForArea(int x, int y, int w, int h);
void resize(Client *c, int x, int y, int w, int h, int interact);
void resizeclient(Client *c, int x, int y, int w, int h);
void resizemouse(const Arg *arg);
void restack(Monitor *m);
void run(void);
void scan(void);
int sendevent(Client *c, Atom proto);
void sendWindowToMonitor(Client *c, Monitor *m);
void setclientstate(Client *c, long state);
void setfocus(Client *c);
void setWindowFullscreen(Client *c, int fullscreen);
void setlayout(const Arg *arg);
void setMasterRatio(const Arg *arg);
void setup(void);
void setWindowUrgent(Client *c, int urg);
void toggleWindowVisibility(Client *c);
void spawn(const Arg *arg);
void tag(const Arg *arg);
void directWindowToMonitor(const Arg *arg);
void toggleDash(const Arg *arg);
void toggleWindowFloating(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void unfocus(Client *c, int setfocus);
void unmanage(Client *c, int destroyed);
void handleWindowUnmap(XEvent *e);
void updateDashPosition(Monitor *m);
void updatebars(void);
void updateclientlist(void);
int updateMonitorGeometry(void);
void updatenumlockmask(void);
void updateWindowSizeHints(Client *c);
void updatestatus(void);
void updateWindowTitle(Client *c);
void updateWindowTypeProps(Client *c);
void updateWindowManagerHints(Client *c);
void view(const Arg *arg);
Client *findClientFromWindow(Window w);
Monitor *findMonitorFromWindow(Window w);
int xerror(Display *dpy, XErrorEvent *ee);
int xerrordummy(Display *dpy, XErrorEvent *ee);
int xerrorstart(Display *dpy, XErrorEvent *ee);
void zoom(const Arg *arg);

/* Layout functions */
void tile(Monitor *m);
void monocle(Monitor *m);
void dwindle(Monitor *m);
void dwindlegaps(Monitor *m);
int shouldscale(Client *c);
void scaleclient(Client *c, int x, int y, int w, int h, float scale);

extern Display *dpy;
extern Monitor *monitors;
extern Monitor *selectedMonitor;
extern Clr **scheme;
extern int bh;
extern Window root;
extern unsigned int numlockmask;

#endif // _ATLASWM_H_endif // _ATLASWM_H_
