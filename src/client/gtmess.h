/*
 *    gtmess.h
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

#ifndef _GTMESS_H_
#define _GTMESS_H_

/*#define LOCK(x) fprintf(stderr, "LOCK(%p)\n", x), pthread_mutex_lock(x)
#define UNLOCK(x) fprintf(stderr, "UNLOCK(%p)\n", x), pthread_mutex_unlock(x)*/

#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)

#define SCL 256

struct cfg_entry {
    char var[256]; /* variable name */
    char sval[256]; /* string value */
    int ival;  /* integer value */
    int type; /* 0 = string; 1 = integer */
    int min, max; /* bounds for integer value */
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
    
    char cfgdir[SCL];
    char datadir[SCL];
    char ca_list[SCL];
    FILE *cfg_fp;
} config_t;

extern char *SP;
extern config_t Config;
extern struct cfg_entry ConfigTbl[];

#endif
