/*
 *    msn.h
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2007  George M. Tzoumas
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

#ifndef _MSN_H_
#define _MSN_H_

#include<iconv.h>
#include<stdio.h>
#include<pthread.h>

#define SXL 1024
#define SML 512
#define SNL 80

#define MSNFTP_PORT 6891

typedef enum {MS_FLN, MS_HDN, MS_NLN, MS_IDL,
                MS_AWY, MS_BSY, MS_BRB, MS_PHN,
                MS_LUN, MS_UNK} msn_stat_t;

typedef struct msn_contact_s {
    char login[SML];    /* FL, RL, AL, BL */
    char nick[SML];     /* FL */
    int dirty;          /* update nickname on server */
    msn_stat_t status;  /* FL */
    int gid;            /* FL only (and for RL represents
                           total number of groups user belongs to) */
    int blocked;        /* FL only */
    time_t tm_last_char; /* FL only: time of last typing notification */
    
    int notify, ignored; /* per-user settings */
    struct msn_contact_s *next;
} msn_contact_t;

typedef struct {
    msn_contact_t *head;
    int count;
} msn_clist_t;

typedef struct msn_group_s {
    char name[SML];
    int gid;
    struct msn_group_s *next;
} msn_group_t;

typedef struct {
    msn_group_t *head;
    int count;
} msn_glist_t;

extern char *msn_stat_name[];
extern char *msn_stat_com[];
extern char msn_stat_char[];
extern iconv_t msn_ic[2];

/* contact list */
msn_contact_t *msn_clist_find(msn_clist_t *q, char *login);
int msn_clist_gcount(msn_clist_t *q, char *login);
int msn_clist_update(msn_clist_t *q, char *login,
                     char *nick, int status, int blocked, int tm_last_char);
void msn_contact_free(msn_contact_t *q);
void msn_clist_free(msn_clist_t *q);
int  msn_clist_rem_grp(msn_clist_t *q, msn_clist_t *rl, int gid);
void msn_clist_rem(msn_clist_t *q, char *login, int gid);
int  msn_clist_load(msn_clist_t *q, FILE *f, int count);
int  msn_clist_save(msn_clist_t *q, FILE *f);
msn_contact_t *msn_clist_add(msn_clist_t *q, int gid, char *login, char *nick);
void msn_clist_cpy(msn_clist_t *dest, msn_clist_t *src);

/* group list */
msn_group_t *msn_glist_find(msn_glist_t *q, int gid);
char *msn_glist_findn(msn_glist_t *q, int gid);
void msn_group_free(msn_group_t *q);
void msn_glist_free(msn_glist_t *q);
void msn_glist_ren(msn_glist_t *q, int gid, char *newname);
void msn_glist_rem(msn_glist_t *q, int gid);
int  msn_glist_load(msn_glist_t *q, FILE *f, int count);
int  msn_glist_save(msn_glist_t *q, FILE *f);
void msn_glist_add(msn_glist_t *q, int gid, char *name);
void msn_glist_cpy(msn_glist_t *dest, msn_glist_t *src);

int is3(char *a, char *b);
void md5hex(char *src, char *hex_output);
void str2url(char *src, char *dest);
void utf8decode(iconv_t ic, char *src, char *dest);
void url2str(char *src, char *dest);

char *msn_error_str(int err);
char *msn_ios_str(int code);

msn_stat_t msn_stat_id(char *s);

int writestr(int fd, char *s);
int msn_png(int fd);
int msn_out(int fd);
int msn_qry(int fd, unsigned int tid, char *hash);
int msn_chg(int fd, unsigned int tid, msn_stat_t status);
int msn_syn(int fd, unsigned int tid, unsigned int ver);
int msn_reg(int fd, unsigned int tid, int gid, char *name);
int msn_rea(int fd, unsigned int tid, char *login, char *name);
int msn_rem(int fd, unsigned int tid, char list, char *login, int gid);
int msn_add(int fd, unsigned int tid, char list, char *login, int gid);
int msn_rmg(int fd, unsigned int tid, int gid);
int msn_adg(int fd, unsigned int tid, char *group);
int msn_gtc(int fd, unsigned int tid, char c);
int msn_blp(int fd, unsigned int tid, char c);
int msn_cvr(int fd, unsigned int tid, char *cvr, char *login);
int msn_login_init(int fd, unsigned int tid, char *login, char *cvr, char *dest);
int msn_login_twn(int fd, unsigned int tid, char *ticket, char *nick);
int msn_msg_typenotif(int fd, unsigned int tid, char *user);
int msn_msg_gtmess(int fd, unsigned int tid, char *cmd, char *args);
int msn_msg_text(int fd, unsigned int tid, char *text);
int msn_msg_finvite(int fd, unsigned int tid, unsigned int cookie, 
                    char *fname, unsigned int size);
int msn_msg_accept2(int fd, unsigned int tid, unsigned int cookie, 
                    char *ip, int port, unsigned int auth_cookie);
int msn_msg_accept(int fd, unsigned int tid, unsigned int cookie);
int msn_msg_cancel(int fd, unsigned int tid, unsigned int cookie, char *code);
int msn_cal(int fd, unsigned int tid, char *user);

#endif
