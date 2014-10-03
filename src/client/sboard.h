/*
 *    sboard.h
 *
 *    gtmess - MSN Messenger client
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

#ifndef _SBOARD_H_
#define _SBOARD_H_

#include<iconv.h>

#include"msn.h"
#include"screen.h"

typedef struct msn_sboard_s {
    char sbaddr[SML];
    char hash[SML];
    int called;

    /* auto updated */
    char sessid[SML]; /* when called */
    msn_clist_t PL; /* participant list */
    char master[SML], mnick[SML];
    int multi;
    int cantype;
    int pending; /* info pending: the user is unaware of the dlg window */

    iconv_t ic; /* character conversion descriptor */
    unsigned int tid;
    TWindow w_dlg, w_prt; /* dialog, participants */
    int w_dlg_top; /* top line (for scrolling) */
    pthread_t thrid;
    int sfd;
    char input[SXL];
    time_t tm_last_char; /* when last char was typed */
    ebox_t EB; /* editbox */
    int plskip; /* PL list line skip (for display) */
    
    struct msn_sboard_s *next, *prev;
} msn_sboard_t;

typedef struct {
    /* double linked circular list */
    msn_sboard_t *head, *start;
    int count;
    
    int out_count;
    pthread_cond_t out_cond; /* to wait for "proper" disconnection */
} msn_sblist_t;

extern pthread_mutex_t SX;
extern msn_sblist_t SL; /* switchboard */
extern pthread_key_t key_sboard;

/* switchboard participants */
void draw_plst(msn_sboard_t *sb, int r);

void draw_sbebox(msn_sboard_t *sb, int r);
void draw_sb(msn_sboard_t *sb, int r);
void draw_dlg(msn_sboard_t *sb, int r);

/* switchboard conversation window */
void dlg(cattr_t attr, const char *fmt, ...);

msn_sboard_t *msn_sblist_add(msn_sblist_t *q, char *sbaddr, char *hash,
                             char *master, char *mnick, int called, char *sessid);
int msn_sblist_rem(msn_sblist_t *q);

#endif
