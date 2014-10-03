/*
 *    menu.h
 *
 *    menu control for curses
 *    Copyright (C) 2006-2007  George M. Tzoumas
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _MENU_H_
#define _MENU_H_

#include<curses.h>

typedef struct { /* no utf8 at this time */
    int index;	 /* position of menu item in list */
    char *text;
    int offset;  /* no. of chars before this entry (for horiz. menus) */
    int hotkey;
    int hotpos;
    int (*callback)(void *);  /* menu function or NULL */
    /* for SENTINEL, callback allows multiple functions (and shortcuts) for a single item
       for other items, the same callback can be used by examining m->index */
    int checked; /* 0 = uncheckable, 1 = unchecked, 3 = checked */
    int checkpos; /* char pos to put an 'x' */
} menuitem_t;

typedef struct {
    int n;          /* number of menu elements */
    int attr;       /* text attribute */
    int msz;        /* requested menu size (width or height), -1 = fit */
    int ms;         /* real (computed) menu size (width or height) */
    int left;       /* number of invisible elements on the left */
    int right;      /* last visible item of menu (depends on left) */
    int cur;        /* selected element */
    int wrap;       /* wrap on edges */
    menuitem_t *item; /* menu elements */
} menu_t;

void mn_init(menu_t *m, int size, int msz, ...);
void mn_toggle(menuitem_t *m);
void mn_setchecked(menuitem_t *m, int expr);
int  mn_ischecked(menuitem_t *m);
void mn_draw(menu_t *m, int row, int col, WINDOW *w);
int mn_keydown(menu_t *m, int key);

#endif
