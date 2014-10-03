/*
 *    gtmess.h
 *
 *    gtmess - MSN Messenger client
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

#ifndef _GTMESS_H_
#define _GTMESS_H_

/*#define LOCK(x) fprintf(stderr, "LOCK(%p)\n", x), pthread_mutex_lock(x)
#define UNLOCK(x) fprintf(stderr, "UNLOCK(%p)\n", x), pthread_mutex_unlock(x)*/

#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)

#define DBG() fprintf(stderr, "DEBUG: %s: %d\n", __FILE__, __LINE__)

#define SCL 256

struct cfg_entry {
    char var[256];  /* variable name */
    char sval[256]; /* string value */
    int ival;       /* integer value */
    int type;       /* 0 = string; 1 = integer */
    int min, max;   /* bounds for integer value */
};

typedef struct {
    int log_traffic;
    int colors;
    int sound;
    int popup;
    int time_user_types;
    char cvr[SCL];
    int cert_prompt;
    int common_name_prompt;
    char console_encoding[SCL];
    char server[SCL];
    char login[SCL];
    char password[SCL];
    int initial_status;
    int online_only;
    int syn_cache;
    int msnftpd;
    int aliases;
    int msg_debug;
    int msg_notify;
    int idle_sec;
    int auto_login;
    int invitable;
    char gtmesscontrol_ignore[SCL];
    int max_nick_len;
    int log_console;
    int notif_aliases;
    int update_nicks;
    char snd_dir[SCL];
    char snd_exec[SCL];
    int snd_redirect;
    int keep_alive;
    
    char cfgdir[SCL];
    char datadir[SCL];
    char ca_list[SCL];
} config_t;

extern char *ZS;
extern config_t Config;
extern struct cfg_entry ConfigTbl[];
extern int SCOLS, SLINES;
extern int utf8_mode;
extern struct tm now_tm;

extern char MyIP[];

void *Malloc(size_t size);
int Write(int fd, void *buf, size_t count);

unsigned int nftid();
char *getnick(char *login, char *nick, int flag);
struct timespec nowplus(int seconds);

void read_syn_cache_data();
void write_syn_cache();

void sched_draw_lst(time_t after);
void redraw_screen(int resize);

void do_ping();
#endif
