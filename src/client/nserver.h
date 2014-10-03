/*
 *    nserver.h
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2009  George M. Tzoumas
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

#define MSN_MAX_GROUP 64

typedef struct {
    /* must be set by user */
    char login[SML], pass[SML];
    char notaddr[SML];

    /* personal state */
    char nick[SML];
    char psm[SML];
    msn_stat_t status;
    int inbox, folders;
    /* privacy mode */
    char BLP; /* all other users: (A)llow; (B)lock */
    char GTC; /* prompt when other users add you: (A)lways; (N)ever */
    /* lists */
    msn_glist_t GL; /* group list */
    msn_clist_t CL; /* contact list (forward, reverse, allow, block) */

    char hlogin[SML]; /* highlighted contact in CL */
    char hgid[SNL];         /* highlighted group in CL */
    int dhid;          /* delta pos. of highlighted contact */
    
    /*msn_clist_t IL; /* initial status list */
    /* unsigned int SYN; /* syn list version */
    int list_count; /* number of LST responses */

	int flskip; /* FL list line skip (for display) */
    pthread_mutex_t lock;
    
    FILE *fp_log; /* log file pointer */
    char fn_log[SML]; /* log file name */
    

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
