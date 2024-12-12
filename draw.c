#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atlas.h"
#include "util.h"

/* Basic drawing context for color allocation */
DrawContext *drw_create(Display *dpy, int screen, Window root, unsigned int w,
                        unsigned int h) {
  DrawContext *drw = ecalloc(1, sizeof(DrawContext));
  drw->dpy = dpy;
  drw->screen = screen;
  drw->root = root;
  drw->w = w;
  drw->h = h;
  drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
  drw->gc = XCreateGC(dpy, root, 0, NULL);
  XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter);
  return drw;
}

void drw_free(DrawContext *drw) {
  XFreePixmap(drw->dpy, drw->drawable);
  XFreeGC(drw->dpy, drw->gc);
  free(drw);
}

/* Color allocation for window borders */
void drw_clr_create(DrawContext *drw, Clr *dest, const char *clrname) {
  if (!drw || !dest || !clrname)
    return;

  if (!XftColorAllocName(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
                         DefaultColormap(drw->dpy, drw->screen), clrname, dest))
    die("error, cannot allocate color '%s'", clrname);
}

/* Create cursors */
CursorWrapper *drw_cur_create(DrawContext *drw, int shape) {
  CursorWrapper *cur;

  if (!drw || !(cur = ecalloc(1, sizeof(CursorWrapper))))
    return NULL;

  cur->cursor = XCreateFontCursor(drw->dpy, shape);
  return cur;
}

void drw_cur_free(DrawContext *drw, CursorWrapper *cursor) {
  if (!cursor)
    return;
  XFreeCursor(drw->dpy, cursor->cursor);
  free(cursor);
}
