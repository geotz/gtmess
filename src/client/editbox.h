/*
 *    editbox.h
 *
 *    editbox control for curses
 *    Copyright (C) 2002-2003  George M. Tzoumas
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

#ifndef _EDITBOX_H_
#define _EDITBOX_H_

#include<curses.h>

#include"hlist.h"

typedef struct {
    int nc, sl; /* max number of chars, current string length; */
    int ii;     /* current editing pos */
    int width;  /* width of screen box */
    int insertmode;
    int left;   /* invisible (scrolled) characters to the left */
    int mask;   /* masked (password) field */
    int esc;    /* in escape mode (i.e. clipboard) */
    int kb;     /* clip-block begin pos */
    int history; /* keep history */
    hlist_t HL;  /* history list */
    
    char *text;
} ebox_t;

void eb_init(ebox_t *e, int nc, int width, char *s);
void eb_settext(ebox_t *e, char *s);
void eb_history_add(ebox_t *e, char *s, int len);
int eb_keydown(ebox_t *e, int key);
void eb_draw(ebox_t *e, WINDOW *w);
void eb_resize(ebox_t *e, int nw);

#endif
