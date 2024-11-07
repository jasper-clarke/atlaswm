// atlaswm.h
#ifndef _ATLASWM_H_
#define _ATLASWM_H_

#include <X11/Xlib.h>

typedef struct Monitor Monitor;
typedef struct Client Client;

struct Client {
  char name[256];
  float mina, maxa;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
  int bw, oldbw;
  unsigned int tags;
  int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
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
  float mfact;
  int nmaster;
  int num;
  int by;             /* bar geometry */
  int mx, my, mw, mh; /* screen size */
  int wx, wy, ww, wh; /* window area  */
  unsigned int seltags;
  unsigned int sellt;
  unsigned int tagset[2];
  int showbar;
  int topbar;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
  const Layout *lt[2];
};

#define HEIGHT(X) ((X)->h + 2 * (X)->bw)
#define WIDTH(X) ((X)->w + 2 * (X)->bw)
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->seltags]))

Client *nexttiled(Client *c);
void resize(Client *c, int x, int y, int w, int h, int interact);

void tile(Monitor *m);
void monocle(Monitor *m);
void dwindle(Monitor *m);
void dwindlegaps(Monitor *m);
int shouldscale(Client *c);
void scaleclient(Client *c, int x, int y, int w, int h, float scale);

#endif // _ATLASWM_H_
