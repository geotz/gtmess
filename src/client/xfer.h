/*
 *    xfer.h
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2011  George M. Tzoumas
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

#ifndef _XFER_H_
#define _XFER_H_

#include"msn.h"
#include"sboard.h"

typedef enum {XF_UNKNOWN, XF_FILE, XF_URL} xclass_t;
typedef enum {XS_UNKNOWN, XS_INVITE, XS_ACCEPT, 
               XS_CANCEL, XS_REJECT, XS_TIMEOUT, XS_FAIL,
               XS_REJECT_NOT_INSTALLED, XS_FTTIMEOUT, XS_OUTBANDCANCEL,
               XS_CONNECT, XS_COMPLETE, XS_ABORT} xstat_t;

/* file transfer (MSNFTP) */
typedef struct {
    char fname[SML];        /* file name */
    unsigned int size;      /* file size */
    unsigned int sofar;     /* bytes transferred so far */
    int port;               /* port of msnftp */
    unsigned int auth_cookie; /* authorization cookie */
    char ipaddr[SML];       /* ip address */
} xfer_file_t;

/* url string transfer (parsed) */
typedef struct {
    char name[SML];
} xfer_url_t;

typedef struct xfer_s {
    xclass_t xclass; /* class of data transfer (i.e. file)*/
    xstat_t status;  /* status of invitiation/transfer */
    char local[SML];  /* local side (login) */
    char remote[SML]; /* remote side (login) */
    int incoming;   /* incoming or outgoing */
    unsigned int inv_cookie; /* invitation cookie */
    msn_sboard_t *sb;   /* switchboard */
    
    pthread_mutex_t tlock; /* thread reference lock */
    pthread_t thrid; /* thread handling this transfer */
    int alive; /* 1 if there exists a thread handling this transfer */
    
    int index; /* index in list */
    
    union {
        xfer_file_t file; /* for file transfers */
        xfer_url_t url; /* for url transfers */
    } data;               /* invitation-specific data */
    struct xfer_s *prev, *next;
} xfer_t;

typedef struct {
    xfer_t *head, *tail, *cur;
    int count;
} xfer_l; 

extern pthread_mutex_t XX;
extern xfer_l XL;
        
xfer_t *xfl_add_file(xfer_l *l, xstat_t status, const char *local, const char *remote, int incoming,
                     unsigned int inv_cookie, msn_sboard_t *sb, 
                     const char *fname, unsigned int fsize);
xfer_t *xfl_add_url(xfer_l *l, const char *local, const char *remote,
                    unsigned int inv_cookie, msn_sboard_t *sb, 
                    const char *url);
xfer_t *xfl_find(xfer_l *l, unsigned int inv_cookie);
void xfl_rem(xfer_l *l, xfer_t *x);
void draw_xfer(int r);
int  xf_keydown(int c);

void *msnftp_client(void *arg);
void msnftp_init();
void msnftp_done();
#endif
