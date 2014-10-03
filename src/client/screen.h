/*
 *    screen.h
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2005  George M. Tzoumas
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

#ifndef _SCREEN_H_
#define _SCREEN_H_

#include<curses.h>
#include<stdarg.h>

#include"../config.h"

#include"editbox.h"
#include"msn.h"

#ifndef HAVE_WRESIZE
#define KEY_RESIZE (-2)
#endif

typedef struct {
    WINDOW *wh;
    int w, h;
    int x, y;
    pthread_mutex_t lock;
} TWindow;

#define W_DLG_W (w_msg.w * 3 / 4)
#define W_DLG_H (SLINES - w_msg.h - 4)
#define W_DLG_X 0
#define W_DLG_Y 1
#define W_PRT_W (w_msg.w - W_DLG_W)
#define W_PRT_H W_DLG_H
#define W_PRT_X (W_DLG_W + W_DLG_X)
#define W_PRT_Y W_DLG_Y
#define W_MSG_LBUF 256
#define W_DLG_LBUF 512

typedef enum {C_NORMAL, C_ERR, C_DBG, C_MSG, C_MNU, C_EBX, C_GRP} cattr_t;

extern int statattrs[], lstatattrs[], attrs[];

extern TWindow w_msg, w_lst, w_back, w_xfer;
extern int w_msg_top;
extern char copyright_str[];

int  get_string(cattr_t attr, int mask, const char *prompt, char *dest);
void vwmsg(TWindow *w, time_t *sbtime, cattr_t attr, FILE *fp_log, const char *fmt, va_list ap);

void msg(cattr_t attr, const char *fmt, ...);
void msg2(cattr_t attr, const char *fmt, ...);
void dlg(cattr_t attr, const char *fmt, ...);

void draw_all();
void draw_rest();
void draw_status(int r);
void draw_time(int r);
void draw_lst(int r);
void draw_msg(int r);

void screen_resize();
void screen_init();

extern int wvis;

#endif
