/*
 *    editbox.h
 *
 *    editbox control for curses
 *    Copyright (C) 2002-2006  George M. Tzoumas
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
#include<wchar.h>
#include"hlist.h"

/* initial editbox buffer size */
#define EBSIZE 1024

typedef struct {
    int nc, sl; /* max number of chars, current string length; */
    int nb;     /* max number of bytes, unicode: nc <= nb, ascii: nc = nb */
    int bl;     /* current string length in bytes (without \0) */
    int ii;     /* current editing pos */
    int width;  /* width of screen box */
    int insertmode;
    int left;   /* invisible (scrolled) characters to the left */
    int mask;   /* masked (password) field */
    int esc;    /* in escape mode (i.e. clipboard) */
    int kb;     /* clip-block begin pos */
    int history; /* keep history */
    int utf8;   /* UTF-8 mode */
    int multi;  /* # of pending characters of multi-byte sequence */
    unsigned char mbseq[7]; /* the multi-byte sequence */
    unsigned char *mbseqp;  /* pointer to the next byte of the mb seq */
    hlist_t HL;  /* history list */
    int mline;  /* multi-line mode */
    int grow;   /* buffer grows dynamically */
    
    unsigned char *text;
    wchar_t *wtext;
} ebox_t;

void eb_init(ebox_t *e, int nc, int width);
void eb_free(ebox_t *e);
void eb_settext(ebox_t *e, char *s);
void eb_history_add(ebox_t *e, char *s, int len);
int eb_keydown(ebox_t *e, int key);
void eb_draw(ebox_t *e, WINDOW *w);
void eb_resize(ebox_t *e, int nw);

#endif
