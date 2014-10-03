/*
 *    nserver.h
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2004  George M. Tzoumas
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

#ifndef _NOTIF_H_
#define _NOTIF_H_

#include"msn.h"

typedef struct {
    /* must be set by user */
    char login[SML], pass[SML];
    char notaddr[SML];

    /* personal state */
    char nick[SML];
    msn_stat_t status;
    int inbox, folders;
    /* privacy mode */
    char BLP; /* all other users: (A)llow; (B)lock */
    char GTC; /* prompt when other users add you: (A)lways; (N)ever */
    /* lists */
    msn_glist_t GL; /* group */
    msn_clist_t FL, RL, AL, BL; /* forward, reverse, allow, block */
    msn_clist_t IL; /* initial status */
    unsigned int SYN; /* syn list version */
    int list_count; /* number of LST responses */
    
    int flskip; /* FL list line skip (for display) */
    pthread_mutex_t lock;

    /* session */
    unsigned int tid;
    int nfd;
    int in_syn; /* SYN in progress */
    pthread_t thrid;
} msn_t;

typedef struct {
    unsigned int ver; /* list version */
    char blp, gtc;
    int fl, rl, al, bl, gl; /* list length */
} syn_hdr_t;

void *msn_ndaemon(void *dummy);
void msn_init(msn_t *msn);

extern msn_t msn;
#endif
