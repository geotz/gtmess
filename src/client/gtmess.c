/*
 *    gtmess.c
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

/* 

TODO LIST
=========

DELETE EVERYTHING AND WRITE THE CLIENT FROM SCRATCH IN C++ :-)


NEXT VERSION
-------------
update manpage with cfg vars (and built-in help)
soft-logout
more colors in sb win <Tibor>

Contact-list menu!

sprintf --> Sprintf (safe!)
add URL... (xfers / strNcpy!!! -- play HOSTILE to others! :->)
check menu cancellations...  update manpage with structure

and keys: F9, backspace/minus ...

resize sub-windows!
restore check for online mode (msn.fd == -1)
avatars!
vertical menu?

auto Be-Right-Back ---> Away
uninvitable when busy
^K-P = push immediately into history buffer and clear line

Invite option on Reverse/Allow list


MAYBE in SOME future version
-----------------------------
check possibility of duplicate switchboard invitations!

fix max_nick_len --> w_prt.w (fit width in participant win) or TRUNCATE!
use xxxx() when xxxx_r() function not available
port to MacOS


utf8encode/decode (alternate version using iconv ONLY)
auto file accept
(MMSNP9 NAT)/proxy pupport
runtime CVR
use wcsrtombs to determine max buf size
shell expansion on editbox!
scripting language/auto send messages (bot)
auto msg response based on status
list speedsearch
clique-block
add option to display all contacts of a group at group list browsing
type notif on sb, too
auto filename complete or file selector
show ETA on transfers!
cache notif/passport server 
MSNFTP: show both IPs or always remote !
support for unavailable connectivity (act as server for receiving a file)
use iconv() at invitation messages, too!
phone numbers
add global ui command line
application/x-msmsgp2p ??
advertise
list menu should support selections (multiple users)
group block/unblock

if I ever have TOO MUCH FREE TIME 
and there are no other features to add :-)
--------------------------------------------------
fix issue with multiple file accepts (after cancel!)
fix issue with number of threads spawned, at msnftpd!
filesize limit is 2GB!!!
should we rely on good luck (for lrand48) ?
do not always assume UTF-8 encoding on server
prove that cleanup handler restores locks :-)
fix issue whether type notif is sent during history browsing
histlen
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <netdb.h>
#include <sys/socket.h>

#include <curses.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#include <iconv.h>
#include <locale.h>

#include "../config.h"

#include "../inty/inty.h"
#include "md5.h"

#include "nserver.h"
#include "screen.h"
#include "editbox.h"
#include "menu.h"
#include "msn.h"
#include "pass.h"
#include "sboard.h"
#include "unotif.h"
#include "xfer.h"
#include "hash_cfg.h"
#include "hash_tbl.h"

#include "gtmess.h"
#include "util.h"

/* var, str_val, int_val, type, int_low, int_high */
struct cfg_entry ConfigTbl[] =
  {
    {"log_traffic", "", 0, 1, 0, 1},
    {"colors", "", 0, 1, 0, 2},
    {"sound", "", 1, 1, 0, 4},
    {"popup", "", 1, 1, 0, 1},
    {"time_user_types", "", 5, 1, 1, 60},
/*    {"cvr", "0x0409 win 4.10 i386 MSNMSGR 5.0.0544 MSMSGS", 0, 0, 0, 0},*/
/*    {"cvr", "0x0409 linux 2.2.4 i386 GTMESS 0.8.4 MSMSGS", 0, 0, 0, 0}, */
    {"cvr", CVRSTRING, 0, 0, 0, 0},
    {"cert_prompt", "", 1, 1, 0, 1},
    {"common_name_prompt", "", 1, 1, 0, 1},
    {"console_encoding", "*locale*", 0, 0, 0, 0},
    {"server", "messenger.hotmail.com", 0, 0, 0, 0},
    {"login", "", 0, 0, 0, 0},
    {"password", "", 0, 0, 0, 0},
    {"initial_status", "", MS_NLN, 1, 0, MS_UNK-1},
    {"online_only", "", 0, 1, 0, 1},
    {"syn_cache", "", 1, 1, 0, 1},
    {"msnftpd", "", MSNFTP_PORT, 1, 0, 65535},
    {"aliases", "", 1, 1, 0, 2},
    {"msg_debug", "", 0, 1, 0, 2},
    {"msg_notify", "", 1, 1, 0, 7200},
    {"idle_sec", "", 180, 1, 0, 7200},
    {"auto_login", "", 1, 1, 0, 1},
    {"invitable", "", 1, 1, 0, 1},
    {"gtmesscontrol_ignore", "", 0, 0, 0, 0},
    {"max_nick_len", "", 0, 1, 0, 1024},
    {"log_console", "", 0, 1, 0, 3},
    {"notif_aliases", "", 0, 1, 0, 1},
    {"update_nicks", "", 0, 1, 0, 2},
    {"snd_dir", "*data*", 0, 0, 0, 0},
    {"snd_exec", "/usr/bin/aplay -Nq %s > /dev/null 2>&1 &", 0, 0, 0, 0},
    {"url_exec", "opera --remote 'openURL(%s,new-page)' >/dev/null 2>&1 &", 0, 0, 0, 0},
    {"keep_alive", "", 0, 1, 0, 7200},
    {"nonotif_status", "", 368, 1, 0, 511},
    {"skip_says", "", 0, 1, 0, 7200},
    {"safe_msg", "", 3, 1, 0, 30}
  };

struct cfg_entry *OConfigTbl;
config_t Config;

int NumConfigVars = 0;

/*extern syn_hdr_t syn_cache;
extern FILE *syn_cache_fp;*/
char *ZS = "";
char MyIP[SML];
char copyright_str[80];

hash_table_t Aliases; /* alias list */

pthread_cond_t cond_out = PTHREAD_COND_INITIALIZER;
struct timeval tv_ping;
int show_ping_result = 0;

/*FILE *syn_cache_fp = NULL;*/
extern FILE *flog;

pthread_mutex_t i_draw_lst_lock;
time_t i_draw_lst_time_start, i_draw_lst_time_end;

time_t keyb_time;
int iamback = 1;

pthread_mutex_t time_lock;
struct tm now_tm;
int i_draw_lst_handled = 1;
pthread_t ithrid;

extern char msg2_str[SML];
extern cattr_t msg2_attr;


void log_out(int panic);
void sboard_leave_all(int panic);

unsigned int _nftid = 1;
pthread_mutex_t nftid_lock;

int ui_active = 0;

unsigned int nftid()
{
    unsigned int r;
    
    LOCK(&nftid_lock);
      r = _nftid++;
    UNLOCK(&nftid_lock);
    return r;
}

unsigned int nftidn(unsigned int n)
{
    unsigned int r;
    
    LOCK(&nftid_lock);
      r = _nftid;
      _nftid += n;
    UNLOCK(&nftid_lock);
    return r;
}

struct timespec nowplus(int seconds)
{
    struct timeval now;
    struct timespec timeout;
    
    gettimeofday(&now, NULL);
    timeout.tv_sec = now.tv_sec + seconds;
    timeout.tv_nsec = now.tv_usec * 1000;
    
    return timeout;
}

void _gtmess_exit(int panic)
{
    LOCK(&scr_lock);
    scr_shutdown = 1;
    UNLOCK(&scr_lock);
    if (Config.log_traffic) fclose(flog);
    
    log_out(panic);
    pthread_cancel(ithrid);
    pass_done();
    unotif_done();
    if (msn_ic[0] != (iconv_t) -1) iconv_close(msn_ic[0]);
    if (msn_ic[1] != (iconv_t) -1) iconv_close(msn_ic[1]);
    if (msn.nfd != -1 || SL.count > 0) usleep(500000);
    erase(); refresh();
    endwin();
    wait(NULL); /* sanity, esp. for beeps! */
/*    if (Config.sound >= 3) beep();*/
}

#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

RETSIGTYPE gtmess_exit_panic() { 
    _gtmess_exit(1);
    _exit(127);
}

void calc_time()
{
    time_t now;
    
    time(&now);
    LOCK(&time_lock);
    localtime_r(&now, &now_tm);
    UNLOCK(&time_lock);
}

void sched_draw_lst(time_t after)
{
    time_t newt;
    
    LOCK(&i_draw_lst_lock);
    newt = time(NULL) + after;
    if (i_draw_lst_handled) {
        i_draw_lst_handled = 0;
        i_draw_lst_time_start = newt;
        i_draw_lst_time_end = newt;
    } else if (newt > i_draw_lst_time_end) i_draw_lst_time_end = newt;
    UNLOCK(&i_draw_lst_lock);
}

void *interval_daemon(void *dummy)
{
    time_t now;
    int last_ping = 0;

    while (1) {
        pthread_testcancel();
        sleep(1);
        pthread_testcancel();
        
        calc_time();
        
        LOCK(&i_draw_lst_lock);
        time(&now);
        if (!i_draw_lst_handled) {
            if (now >= i_draw_lst_time_start) draw_lst(1);
            if (now >= i_draw_lst_time_end) i_draw_lst_handled = 1;
        }
        UNLOCK(&i_draw_lst_lock);
        
        draw_time(1);
        
        if (msn.nfd != -1 && Config.idle_sec && msn.status == MS_NLN &&
            Config.idle_sec + keyb_time < now) {
                msn_chg(msn.nfd, nftid(), MS_IDL);
                keyb_time = now;
                iamback = 0;
        }
        
        if (Config.keep_alive && now - last_ping > Config.keep_alive && msn.nfd != -1) {
            do_ping();
            last_ping = now;
        }
    }
}

void interval_init()
{
    pthread_mutex_init(&i_draw_lst_lock, NULL);
    pthread_create(&ithrid, NULL, interval_daemon, NULL);
    pthread_detach(ithrid);
}

void panic(char *s)
{
    _gtmess_exit(1);
    fprintf(stderr, "FATAL ERROR: %s\n", s);
    _exit(128);
}

char *getalias(char *login)
{
    char *alias;
    
    alias = hash_tbl_find(&Aliases, login);
    if (alias == NULL) return "<none>";
    else return alias;
}

char *getnick(char *login, char *nick, int flag)
{
    char *alias;
    int maxlen;
    
    if (login == NULL || flag == 0) return nick;
    if ((alias = hash_tbl_find(&Aliases, login)) != NULL) {
        maxlen = Config.max_nick_len;
        if (maxlen == 0) maxlen = w_lst.w-3;
        if (flag == 2 || 
            strstr(nick, login) != NULL || strlen(nick) > maxlen) return alias;
        else return nick;
    } else {
        return nick;
    }
}

/*int read_syn_cache_hdr()
{
    char fn[SML];
    
    sprintf(fn, "%s/%s/syn", Config.cfgdir, msn.login);
    if ((syn_cache_fp = fopen(fn, "r")) != NULL &&
        fread(&syn_cache, sizeof(syn_hdr_t), 1, syn_cache_fp) == 1) {
            return 0;
    }
    syn_cache.ver = 1024;
    return -1;
}

void read_syn_cache_data()
{
    msn_contact_t *p, *q;
    
    msn.BLP = syn_cache.blp;
    msn.GTC = syn_cache.gtc;
    msn_clist_load(&msn.FL, syn_cache_fp, syn_cache.fl);
    msn_clist_load(&msn.AL, syn_cache_fp, syn_cache.al);
    msn_clist_load(&msn.BL, syn_cache_fp, syn_cache.bl);
    msn_clist_load(&msn.RL, syn_cache_fp, syn_cache.rl);
    msn_glist_load(&msn.GL, syn_cache_fp, syn_cache.gl);
    if (syn_cache_fp != NULL) fclose(syn_cache_fp);
    
    for (q = msn.RL.head; q != NULL; q = q->next)
        if ((p = msn_clist_find(&msn.FL, q->login)) == NULL) {
            if (msn.GTC == 'A' && msn_clist_find(&msn.AL, q->login) == NULL &&
                msn_clist_find(&msn.BL, q->login) == NULL)
                    msg(C_MSG, "%s <%s> has added you to his/her contact list\n", getnick(q->login, q->nick), q->login);
        }
    msg(C_MSG, "FL/AL/BL/RL contacts: %d/%d/%d/%d\n", msn.FL.count, msn.AL.count, msn.BL.count, msn.RL.count);
    for (p = msn.FL.head; p != NULL; p = p->next)
        if (msn_clist_find(&msn.RL, p->login) == NULL)
            * bug: multiple same warnings, when user on multiple groups! *
            msg(C_MSG, "%s <%s> has removed you from his/her contact list\n",
                getnick(p->login, p->nick), p->login);
    * update FL with IL *
    for (p = msn.IL.head; p != NULL; p = p->next)
        msn_clist_update(&msn.FL, p->login, p->nick, p->status, -1, -1);
    msn_clist_free(&msn.IL);

}

void write_syn_cache()
{
    FILE *f;
    char fn[SML];
    
    sprintf(fn, "%s/%s/syn", Config.cfgdir, msn.login);
    syn_cache.ver = msn.SYN;
    syn_cache.blp = msn.BLP;
    syn_cache.gtc = msn.GTC;
    syn_cache.fl = msn.FL.count;
    syn_cache.al = msn.AL.count;
    syn_cache.bl = msn.BL.count;
    syn_cache.rl = msn.RL.count;
    syn_cache.gl = msn.GL.count;
    if ((f = fopen(fn, "w")) != NULL) {
        fwrite(&syn_cache, sizeof(syn_cache), 1, f);
        msn_clist_save(&msn.FL, f);
        msn_clist_save(&msn.AL, f);
        msn_clist_save(&msn.BL, f);
        msn_clist_save(&msn.RL, f);
        msn_glist_save(&msn.GL, f);
        fclose(f);
    }
}*/

int do_connect(char *server)
{
    int sfd;

    if ((sfd = ConnectToServer(server, 1863)) < 0)
        msg(C_ERR, "%s\n", strerror(errno));
    else msg(C_MSG, "Connected\n");
    return sfd;
}

int do_login()
{
    int r;
    char hash[SXL];
    char dalogin[SXL], ticket[SXL];
    char udir[SML];

    msg(C_NORMAL, "Connecting to server %s ...\n", msn.notaddr);
    msn.nfd = do_connect(msn.notaddr);
    if (msn.nfd < 0) return -1;
    GetMyIP(msn.nfd, MyIP);

    if ((r = msn_login_init(msn.nfd, nftidn(3), msn.login, Config.cvr, hash)) == 1) {
        Strcpy(msn.notaddr, hash, SML);
        msg(C_MSG, "Notification server is %s\n", msn.notaddr);
        return 1;
    }
                
    if (r != 0) {
        msg(C_ERR, "%s\n", msn_ios_str(r));
        return -3;
    }

    if (get_login_server(dalogin) != NULL) {
        if ((r = get_ticket(dalogin, msn.login, msn.pass, hash, ticket, SXL)) == 0) {
            if ((r = msn_login_twn(msn.nfd, nftid(), ticket, msn.nick)) != 0) {
                msg(C_ERR, "%s\n", msn_ios_str(r));
                return -3;
            }
        } else {
            if (r == 400) msg(C_ERR, "PASSPORT: authentication failed\n");
            msg(C_ERR, "PASSPORT: could not get a ticket\n");
            return -400;
        }
    } else {
        msg(C_ERR, "PASSPORT: Could not get a login server\n");
        return -4;
    }
    draw_status(0);
    msg(C_MSG, "Login successful\n");
    
    /* create user data dir */
    sprintf(udir, "%s/%s", Config.cfgdir, msn.login);
    mkdir(udir, 0700);
    sprintf(udir, "%s/%s/log", Config.cfgdir, msn.login);
    mkdir(udir, 0700);
    
    pthread_create(&msn.thrid, NULL, msn_ndaemon, (void *) NULL);
/*    pthread_detach(msn.thrid);*/

    /* sync lists */
/*    read_syn_cache_hdr();
    msn_syn(msn.nfd, nftid(), syn_cache.ver);*/
            
    msn_syn(msn.nfd, nftid(), 1);

    return 0;
}

void sendstr(char *s)
{
    Write(msn.nfd, s, strlen(s));
}

void redraw_screen(int resize)
{
    LOCK(&msn.lock);
    LOCK(&SX);
    LOCK(&scr_lock);
      erase();
      refresh();
/*      redrawwin(curscr);*/
      if (resize) screen_resize();
      draw_all(); 
    UNLOCK(&scr_lock);
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
    msg2(msg2_attr, "%s", msg2_str);
}

void print_usage(char *argv0)
{
    fprintf(stderr, "USAGE:\n"
                    "\t%s [[-<option><optval>] ...]\n\n"
                    "OPTIONS:\n"
                    "-h,\n"
                    "--help\t\t\tthis help text\n"
                    "-O<variable>=<value>\tset config var\n"
                    "--version\t\tprint program version and exit\n"
                    "\n"
                    "config vars, <n>=int, <s>=str, <b>=bool(1/0), []=default\n"
                    "--------------------------------------------------------\n"
                    "aliases=<n>\t\tuse contact aliases [1], 0=never, 1=smart, 2=always\n"
                    "auto_login=<b>\t\tauto-login on startup [1]\n"
                    "cert_prompt=<b>\t\tprompt on unverified certificates (SSL) [1]\n"
                    "colors=<n>\t\tcolor mode [0]\n\t\t\t0=auto, 1=color, 2=mono\n"
                    "common_name_prompt=<b>\tprompt on name mismatch (SSL) [1]\n"
                    "console_encoding=<s>\tconsole encoding (iconv -l) [*locale*]\n"
                    "cvr=<s>\t\t\tconsumer versioning (CVR) string\n"
                    "gtmesscontrol_ignore=<s>ignore gtmess-specific messages, <s>=*all* or\n"
                    "\t\t\tignore only messages that are substrings of <s>\n"
                    "idle_sec=<n>\t\tseconds before auto-idle status [180], 0=never, <= 7200\n"
                    "initial_status=<n>\tinitial status [2], 0 <= <n> <= 8 for\n"
                    "\t\t\tOffline, Appear_Offline, Online, Idle, Away,\n"
                    "\t\t\tBusy, Be_Right_Back, On_the_Phone, Out_to_Lunch\n"
                    "invitable=<b>\t\tallow others to invite you to conversations [1]\n"
                    "keep_alive=<n>\t\tkeep connection alive by regular PNG every <n> sec\n"
                    "login=<s>\t\tdefault login account\n"
                    "log_traffic=<b>\t\tlog network traffic [0]\n"
                    "log_console=<n>\t\tlog console messages [0], 0=none, 1=sb, 2=ns, 3=both\n"
                    "max_nick_len=<n>\tmax nickname length before use of alias [0], 0=fit\n"
                    "msg_debug=<n>\t\thandle unknown msgs [0], 0 = ignore,\n"
                    "\t\t\t1 = show type, 2 = show whole msg\n"
                    "msg_notify=<n>\t\textra notification on received messages [1],\n"
                    "\t\t\t0=never, 1=when idle, or when <n> seconds have passed\n"
                    "msnftpd=<n>\t\tport of msnftp server [6891], 0=disable\n"
                    "nonotif_status=<n>\t\tdisable snd/popup notification in these statuses\n"
                    "notif_aliases=<n>\taliases in notif win [0], 0=never, 1=smart, 2=always\n"
                    "online_only=<b>\t\tshow only online contacts [0]\n"
                    "password=<s>\t\tdefault password\n"
                    "popup=<b>\t\tenable/disable popup notification window [1]\n"
                    "safe_msg=<n>\t\tprevents inadvertently close chat window  with\n"
                    "\t\t\ttoo recent messages (until <n>-seconds ago) [3]\n"
                    "server=<s>[:<n>]\tdispatch/notification server (optional port)\n"
                    "skip_says=<n>\t\tskip redundant 'says:' messages if less than\n"
                    "\t\t\t<n> seconds apart, 0 <= <n> <= 7200 [0]\n"
                    "snd_dir=<s>\t\tdirectory for sound effects [*data*]\n"
                    "snd_exec=<s>\t\tcommand line for sound player\n"
                    "\t\t\t[%s]\n"
                    "sound=<n>\t\tsound mode [1]\n"
                    "\t\t\t<n> = 0:no snd, 1:beep, 2:sound effects, 3:pcspk, 4:pcspkr linux\n"
                    "syn_cache=<b>\t\tcache contact/group lists [0]\n"
                    "time_user_types=<n>\tinterval of typing notifications [%d]\n"
                    "update_nicks=<n>\tupdate nicknames on server [0]\n"
                    "\t\t\t0=never, 1=smart, 2=always\n"
                    "url_exec=<s>\t\tcommand line for url browser\n"
                    "\n"
                    "EXAMPLE:\n"
                    "\t%s -Ologin=george -Opassword=secret123 -Oinitial_status=1\n",
             argv0, ConfigTbl[28].sval, ConfigTbl[4].ival, argv0);
}

int play_menu(menu_t *m)
{
    int key, sel;

    while (1) {
        LOCK(&scr_lock);
        mn_draw(m, SLINES-1, 0, stdscr);
        UNLOCK(&scr_lock);
    	key = getch();
	sel = mn_keydown(m, key);
        
        /* accept choice or exit whole menu */
        if ((sel == 2) || (key == KEY_F(9))) return 2;
        
        /* cancel menu */
        if (sel == -1) return -1;
        
        /* special (redraw) event */
        if (sel == 0) {
            if (key == KEY_RESIZE) {
                redraw_screen(1);
                m->cur = m->left = 0;
            } else {
                if (key != ERR) beep();
            }
        }
        
        /* if sel == 1 then menu stays (exit submenu) */
    }
}

menu_t menu_YN;

int export_nicks(void *arg)
{
    FILE *f;
    char fn[SML];
/*    char ulogin[SML], unick[SML];*/
    int c;
    msn_contact_t *p;

    sprintf(fn, "%s/aliases", Config.cfgdir);
    if ((f = fopen(fn, "r")) != NULL) {
        fclose(f);
        msg(C_ERR, "Overwrite existing aliases file?\n");
        menu_YN.cur = 1;
        if (play_menu(&menu_YN) == 2) switch(menu_YN.cur) {
            case 1: /* No */
                msg(C_ERR, "Alias export cancelled\n");
                return 2;
            case 0: /* Yes */
                break;
            default:
                return 2;
        } else return 2;
    }
    f = fopen(fn, "w");
    if (f == NULL) {
        msg(C_ERR, "Failed to create output file\n");
        return 2;
    }
    
    for (p = msn.FL.head, c = 0; p != NULL; p = p->next) {
/*        str2url(p->login, ulogin);
        str2url(p->nick, unick);*/
        fprintf(f, "%s %s\n", p->login, p->nick);
        c++;
    }
    fclose(f);
    msg(C_DBG, "Exported %d alias%s\n", c, (c!=1)?"es": "");
    
    return 2;
}

int parse_cfg_entry(char *s)
{
    char ds[SXL], *eq;
    char variable[SML];
    int len;
    int ival;
    struct hct_entry *h;
    struct cfg_entry *e;
    
    strncpy(ds, s, SXL);
    ds[SXL-1] = 0;
    variable[0] = 0;
    if ((eq = strchr(ds, '=')) != NULL && (eq - s < SML)) {
        *eq = 0;
        sscanf(ds, "%s", variable);
        len = strlen(variable);
        h = in_word_set(variable, len);
        if (h != NULL) {
            e = &ConfigTbl[h->id];
            if (e->type == 0) { /* string */
                if (strcmp(variable, "password") == 0) 
                    cipher_string((unsigned char *) eq+1, (unsigned char *) "gTme$$", 0);
                else if ((strcmp(variable, "snd_exec") == 0) || (strcmp(variable, "url_exec") == 0)) {
                    if (!valid_shell_string(eq+1)) {
                        beep();
                        msg(C_ERR, "parse_cfg_entry(%s): invalid string, should contain exactly one %%s\n", variable);
                        return 0;
                    }
                }
                strncpy(e->sval, eq+1, 255);
                e->sval[255] = 0;
                if (ui_active && strcmp(variable, "password") != 0) 
                    msg(C_MSG, "Variable `%s' has been set to `%s'\n", variable, e->sval);
                return 0;
            } else { /* integer */
                if (sscanf(eq+1, "%d", &ival) == 1) {
                    if (ival < e->min) {
                        msg(C_ERR, "parse_cfg_entry(%s): value too small, %d assumed\n", variable, e->min);
                        ival = e->min;
                    } else if (ival > e->max) {
                        msg(C_ERR, "parse_cfg_entry(%s): value too big, %d assumed\n", variable, e->max);
                        ival = e->max;
                    }
                    e->ival = ival;
                    if (ui_active) msg(C_MSG, "Variable `%s' has been set to %d\n", variable, ival);
                    return 0;
                } else msg(C_ERR, "parse_cfg_entry(): integer format expected (%s=?)\n", variable);
            }
        } else msg(C_ERR, "parse_cfg_entry(): invalid variable name `%s'\n", variable);
    } else msg(C_ERR, "parse_cfg_entry(): wrong format (<variable>=<value> expected)\n");
    return -1;
}

void parse_config(int argc, char **argv)
{
    char **arg;
    char tmp[SXL], *nl;
    FILE *cfg_fp;
    
    sprintf(tmp, "%s/config", Config.cfgdir);
    cfg_fp = fopen(tmp, "r");
    if (cfg_fp != NULL) {
        while (fgets(tmp, SXL, cfg_fp) != NULL) {
            if (tmp[0] == '#' || tmp[0] == '\n') continue;
            if ((nl = strchr(tmp, '\n')) != NULL) *nl = 0;
            parse_cfg_entry(tmp);
        }
        fclose(cfg_fp);
    }
    
    for (arg = argv + 1; --argc; arg++) {
        if (arg[0][0] == '-') switch (arg[0][1]) {
            case 'h':
                print_usage(argv[0]);
                _exit(0);
                break;
            case 'O':
                if (parse_cfg_entry(&arg[0][2]) != 0) _exit(1);
                break;
            case '-':
                if (strcmp(&arg[0][2], "help") == 0) {
                    print_usage(argv[0]);
                    _exit(0);
                    break;
                } else if (strcmp(&arg[0][2], "version") == 0) {
                    fprintf(stderr, "CVR = %s\n", CVRSTRING);
                    _exit(0);
                    break;
                }
            default:
                fprintf(stderr, "unknown option: %s (-h for help)\n", *arg);
                _exit(1);
        }
    }
}

int file_exists(char *path)
{
    struct stat buf;
    return stat(path, &buf) == 0;
}

void get_lc_ctype(char *dest, size_t n)
{
    char loc[SML], *s;
    
    s = setlocale(LC_CTYPE, "");
    if (s != NULL) {
        Strcpy(loc, s, SML);
        s = strchr(loc, '@');
        if (s != NULL) *s = 0;
        s = strchr(loc, '.');
        if (s != NULL) {
            Strcpy(dest, s+1, n);
            return;
        }
    }
    Strcpy(dest, "ISO8859-1", n);
}

void get_home_dir(char *dest, size_t n)
{
    char *s;
    if ((s = getenv("HOME")) != NULL)
        Strcpy(dest, s, n);
    else Strcpy(dest, ".", n);
}

void config_init()
{
    int c = 0;
    
    Config.log_traffic = ConfigTbl[c++].ival;
    Config.colors = ConfigTbl[c++].ival;
    Config.sound = ConfigTbl[c++].ival;
    Config.popup = ConfigTbl[c++].ival;
    Config.time_user_types = ConfigTbl[c++].ival;
    Strcpy(Config.cvr, ConfigTbl[c++].sval, SCL);
    Config.cert_prompt = ConfigTbl[c++].ival;
    Config.common_name_prompt = ConfigTbl[c++].ival;
    Strcpy(Config.console_encoding, ConfigTbl[c++].sval, SCL);
    Strcpy(Config.server, ConfigTbl[c++].sval, SCL);
    Strcpy(Config.login, ConfigTbl[c++].sval, SCL);
    Strcpy(Config.password, ConfigTbl[c++].sval, SCL);
    Config.initial_status = ConfigTbl[c++].ival;
    Config.online_only = ConfigTbl[c++].ival;
    Config.syn_cache = ConfigTbl[c++].ival;
    Config.msnftpd = ConfigTbl[c++].ival;
    Config.aliases = ConfigTbl[c++].ival;
    Config.msg_debug = ConfigTbl[c++].ival;
    Config.msg_notify = ConfigTbl[c++].ival;
    Config.idle_sec = ConfigTbl[c++].ival;
    Config.auto_login = ConfigTbl[c++].ival;
    Config.invitable = ConfigTbl[c++].ival;
    Strcpy(Config.gtmesscontrol_ignore, ConfigTbl[c++].sval, SCL);
    Config.max_nick_len = ConfigTbl[c++].ival;
    Config.log_console = ConfigTbl[c++].ival;
    Config.notif_aliases = ConfigTbl[c++].ival;
    Config.update_nicks = ConfigTbl[c++].ival;
    Strcpy(Config.snd_dir, ConfigTbl[c++].sval, SCL);
    Strcpy(Config.snd_exec, ConfigTbl[c++].sval, SCL);
    Strcpy(Config.url_exec, ConfigTbl[c++].sval, SCL);
    Config.keep_alive = ConfigTbl[c++].ival;
    Config.nonotif_status = ConfigTbl[c++].ival;
    Config.skip_says = ConfigTbl[c++].ival;
    Config.safe_msg = ConfigTbl[c++].ival;
    NumConfigVars = c;
    
    sprintf(Config.datadir, "%s", DATADIR);
    
    if (strcmp(Config.snd_dir, "*data*") == 0) 
        sprintf(Config.snd_dir, "%s/snd", Config.datadir);
    
    Strcpy(Config.ca_list, "./root.pem", SCL);
    if (!file_exists(Config.ca_list)) {
        sprintf(Config.ca_list, "%s/root.pem", Config.cfgdir);
        if (!file_exists(Config.ca_list)) 
            sprintf(Config.ca_list, "%s/root.pem", Config.datadir);
    }

    if (msn.nfd == -1) {
        if (Config.login[0] && strchr(Config.login, '@') == NULL) strcat(Config.login, "@hotmail.com");
        Strcpy(msn.notaddr, Config.server, SML);
        Strcpy(msn.login, Config.login, SML);
        Strcpy(msn.pass, Config.password, SML);
    }
    
    if (msn_ic[0] != (iconv_t) -1) iconv_close(msn_ic[0]);
    if (msn_ic[1] != (iconv_t) -1) iconv_close(msn_ic[1]);
    
    if (strcmp(Config.console_encoding, "*locale*") == 0)
        get_lc_ctype(Config.console_encoding, SCL);

    if (strcmp(Config.console_encoding, "UTF-8") == 0) {
        msn_ic[0] = msn_ic[1] = (iconv_t) -1;
        utf8_mode = 1;
    } else {
        /* from UTF-8 to console */
        msn_ic[0] = iconv_open(Config.console_encoding, "UTF-8");
        /* from console to UTF-8 */
        msn_ic[1] = iconv_open("UTF-8", Config.console_encoding);
        utf8_mode = 0;
    }
    
    if (Config.log_traffic) {
        char logfn[SML];
        sprintf(logfn, "%s/msn.log", Config.cfgdir);
        if ((flog = fopen(logfn, "a")) == NULL) Config.log_traffic = 0;
    }
}

int menu_status_CB(void *arg)
{
    menuitem_t *m = (menuitem_t *) arg;
    switch (m->index) {
        case 0: msn_chg(msn.nfd, nftid(), MS_NLN); return 2;
        case 1: msn_chg(msn.nfd, nftid(), MS_IDL); return 2;
        case 2: msn_chg(msn.nfd, nftid(), MS_AWY); return 2;
        case 3: msn_chg(msn.nfd, nftid(), MS_BSY); return 2;
        case 4: msn_chg(msn.nfd, nftid(), MS_BRB); return 2;
        case 5: msn_chg(msn.nfd, nftid(), MS_PHN); return 2;
        case 6: msn_chg(msn.nfd, nftid(), MS_LUN); return 2;
        case 7: msn_chg(msn.nfd, nftid(), MS_HDN); return 2;
        case 8: log_out(0); return 2;
    }
    return 0;
}

int cfilt_online(msn_contact_t *p)
{
    return p->status >= MS_NLN;
}

int menu_clist(pthread_mutex_t *lock, msn_clist_t *q, msn_contact_t *con, int showgrp, 
               int (*filter)(msn_contact_t *p))
{
    int c, i, j, count;
    msn_clist_t p;
    msn_contact_t **a, *r;

    if (lock != NULL) LOCK(lock);
    if (q->count == 0) {
        msg(C_ERR, "List is empty\n");
        if (lock != NULL) UNLOCK(lock);
        return -1;
    }
    msn_clist_cpy(&p, q);
    if (lock != NULL) UNLOCK(lock);
    a = (msn_contact_t **) Malloc(p.count * sizeof(msn_contact_t *));
    for (r = q->head, count = 0; r != NULL; r = r->next) {
        if (filter == NULL || (*filter)(r)) a[count++] = r;
    }
    if (count == 0) {
        msn_clist_free(&p);
        free(a);
        msg(C_ERR, "No online contacts\n");
        return -1;
    }
    i = c = 0;
    while (1) {
        if (showgrp)
            msg2(C_MNU, "[%d] (%d/%d) %s <%s>",
                    a[i]->gid, i+1, count, getnick(a[i]->login, a[i]->nick, Config.aliases), a[i]->login);
        else
            msg2(C_MNU, "(%d/%d) %s <%s>", i+1, count, getnick(a[i]->login, a[i]->nick, Config.aliases), a[i]->login);
        if (c == ' ' || c == '\r') break;
        switch (c = getch()) {
            case KEY_RESIZE: redraw_screen(1); break;
            case ']':
            case KEY_RIGHT:
            case KEY_DOWN: i++; if (i == count) i = 0; break;
            case '[':
            case KEY_LEFT:
            case KEY_UP: i--; if (i < 0) i = count - 1; break;
            case '{':
            case KEY_HOME: i = 0; break;
            case '}':
            case KEY_END: i = count - 1; break;
            case 'l':
                if (showgrp) for (j = 0; j < count; j++)
                    msg(C_NORMAL, "%2d. [%d] %s <%s>\n", j + 1, a[j]->gid, getnick(a[j]->login, a[j]->nick, Config.aliases), a[j]->login);
                else for (j = 0; j < count; j++)
                    msg(C_NORMAL, "%2d. %s <%s>\n", j + 1, getnick(a[j]->login, a[j]->nick, Config.aliases), a[j]->login);
                break;
            case 'q':
                msg(C_NORMAL, "NICK: %s\n", a[i]->nick);
                msg(C_NORMAL, "ALIAS: %s\n", getalias(a[i]->login));
                msg(C_NORMAL, "LOGIN: %s\n", a[i]->login);
                if (q == &msn.FL) {
                    msg(C_NORMAL, "STATUS: %s\n", msn_stat_name[a[i]->status]);
                    msg(C_NORMAL, "BLOCKED: %s\n", a[i]->blocked? "yes": "no");
                    msg(C_NORMAL, "GROUP: [%d] - %s\n", a[i]->gid, msn_glist_findn(&msn.GL, a[i]->gid));
                }
                if (q == &msn.RL) msg(C_NORMAL, "GROUPS: %d\n", a[i]->gid);
                break;
            case 27:
            case 8:
            case KEY_BACKSPACE: i = -1; break;
        }
        if (i == -1) break;
    }
    if (i >= 0) memcpy(con, a[i], sizeof(msn_contact_t));
    msn_clist_free(&p);
    free(a);
    return i;
}

int menu_glist(pthread_mutex_t *lock, msn_glist_t *q, msn_group_t *grp)
{
    int c, i, j;
    msn_glist_t p;
    msn_group_t **a, *r;

    if (lock != NULL) LOCK(lock);
    if (q->count == 0) {
        msg(C_ERR, "List is empty\n");
        if (lock != NULL) UNLOCK(lock);
        return -1;
    }
    msn_glist_cpy(&p, q);
    if (lock != NULL) UNLOCK(lock);
    a = (msn_group_t **) Malloc(p.count * sizeof(msn_group_t *));
    for (r = q->head, i = 0; r != NULL; r = r->next, i++) a[i] = r;
    i = c = 0;
    while (1) {
        msg2(C_MNU, "(%d/%d) [%d] - %s", i+1, p.count, a[i]->gid, a[i]->name);
        if (c == ' ' || c == '\r') break;
        switch (c = getch()) {
            case KEY_RESIZE: redraw_screen(1); break;
            case ']':
            case KEY_RIGHT:
            case KEY_DOWN: i++; if (i == p.count) i = 0; break;
            case '[':
            case KEY_LEFT:
            case KEY_UP: i--; if (i < 0) i = p.count - 1; break;
            case '{':
            case KEY_HOME: i = 0; break;
            case '}':
            case KEY_END: i = p.count - 1; break;
            case 'q':
            case 'Q':
                for (j = 0; j < p.count; j++)
                    msg(C_NORMAL, "%2d. [%d] - %s\n", j+1, a[j]->gid, a[j]->name);
                break;
            case 27:
            case 8:
            case KEY_BACKSPACE: i = -1; break;
        }
        if (i == -1) break;
    }
    if (i >= 0) memcpy(grp, a[i], sizeof(msn_group_t));
    msn_glist_free(&p);
    free(a);
    return i;
}

void menu_change_gname(msn_group_t *q)
{
    char newn[SML];

    Strcpy(newn, q->name, SML);
    if (get_string(C_EBX, 0, "New name: ", newn, SML))
        msn_reg(msn.nfd, nftid(), q->gid, newn);
}

int menu_change_name(char *login, char *old)
{
    char newn[SML];

    Strcpy(newn, old, SML);
    if (get_string(C_EBX, 0, "New name: ", newn, SML)) {
        msn_rea(msn.nfd, nftid(), login, newn);
        return 1;
    } else return 0;
}

void do_invite(msn_contact_t *p)
{
    LOCK(&SX);
    if (SL.head == NULL || SL.head->sfd == -1) {
        UNLOCK(&SX);
        msg(C_ERR, "Not in a switchboard session\n");
    } else {
        msn_cal(SL.head->sfd, SL.head->tid++, p->login);
        UNLOCK(&SX);
    }
}

int menu_invite()
{
    msn_contact_t p;
    if (menu_clist(&msn.lock, &msn.FL, &p, 1, cfilt_online) >= 0) {
        do_invite(&p);
        return 1;
    }
    return 0;
}

menu_t menu_RLp;

int menu_srv_RL(void *arg)
{
    if (msn.nfd == -1) {
        msg(C_ERR, "You must be logged in for this option to work\n");
        return 1;
    } else if (play_menu(&menu_RLp) == 2) switch (menu_RLp.cur) {
        case 0: /* Always */
            msn_gtc(msn.nfd, nftid(), 'A');
            break;
        case 1: /* Never */
            msn_gtc(msn.nfd, nftid(), 'N');
            break;
        case 2: /* Query */
            msg(C_NORMAL, "Prompt when other users add you: %s\n", msn.GTC == 'N'? "NEVER": "ALWAYS");
            break;
    }
    return 2;
}

menu_t menu_AO;

int menu_srv_AO(void *arg)
{
    if (msn.nfd == -1) {
        msg(C_ERR, "You must be logged in for this option to work\n");
        return 1;
    } else if (play_menu(&menu_AO) == 2) switch (menu_AO.cur) {
        case 0: /* Allow */
            msn_blp(msn.nfd, nftid(), 'A');
            break;
        case 1: /* Block */
            msn_blp(msn.nfd, nftid(), 'B');
            break;
        case 2: /* Query */
            msg(C_NORMAL, "All other users: %s\n", msn.BLP == 'B'? "BLOCKED": "ALLOWED");
            break;
    }
    return 2;
}

int menu_opt_VAR(void *arg)
{
    char optline[SML];
    optline[0] = 0;
    if (get_string(C_EBX, 0, "<var>=<value>:", optline, SML)) {
        parse_cfg_entry(optline);
        config_init();
        redraw_screen(0);
    }
    return 1;
}

int menu_opt_QRY(void *arg)
{
    int i;

    msg(C_DBG, "-- begin cfg --\n");
    for (i = 0; i < NumConfigVars; i++) {
        if (strcmp(ConfigTbl[i].var, "password") == 0) continue; /* security */
        if (ConfigTbl[i].type == 0) {/* string */
            msg(C_DBG, "%s=%s\n", ConfigTbl[i].var, ConfigTbl[i].sval);
            if (strcmp(ConfigTbl[i].var, "console_encoding") == 0 &&
                strcmp(ConfigTbl[i].sval, "*locale*") == 0)
                    msg(C_DBG, "*locale*=%s\n", Config.console_encoding);
        } else                     /* int */ 
            msg(C_DBG, "%s=%d\n", ConfigTbl[i].var, ConfigTbl[i].ival);
    }
    msg(C_DBG, "-- end cfg --\n");
    return 2;
}

int menu_opt_WR(void *arg)
{
    char tmp[SML];
    FILE *cfg_fp;

    sprintf(tmp, "%s/config", Config.cfgdir);
    cfg_fp = fopen(tmp, "w");
    if (cfg_fp == NULL) msg(C_ERR, "Cannot open config file for writing\n");
    else {
        int i;
        msg(C_DBG, "-- begin cfg (written) --\n");
        for (i = 0; i < NumConfigVars; i++) {
            if (strcmp(ConfigTbl[i].var, "password") == 0) continue; /* security */
            if (ConfigTbl[i].type == 0) { /* string */
                if(strcmp(ConfigTbl[i].sval, OConfigTbl[i].sval) != 0) {
                    fprintf(cfg_fp, "%s=%s\n", ConfigTbl[i].var, ConfigTbl[i].sval);
                    msg(C_DBG, "%s=%s\n", ConfigTbl[i].var, ConfigTbl[i].sval);
                }
            } else {                     /* int */ 
                if (ConfigTbl[i].ival != OConfigTbl[i].ival) {
                    fprintf(cfg_fp, "%s=%d\n", ConfigTbl[i].var, ConfigTbl[i].ival);
                    msg(C_DBG, "%s=%d\n", ConfigTbl[i].var, ConfigTbl[i].ival);
                }
            }
        }
        fclose(cfg_fp);
        msg(C_DBG, "-- end cfg (written) --\n");
    }
    return 2;
}

int menu_opt_SND(void *arg)
{
    msg(C_DBG, "Press 1-6 for this option\n");
    return 1;
}

int menu_opt_PW(void *arg)
{
    char note[SML] = {0};
    if (get_string(C_EBX, 1, "Password:", note, SML) && note[0]) {
        cipher_string((unsigned char *) note, (unsigned char *) "gTme$$", 1);
        msg(C_ERR, "%s\n", note);
        return 2;
    }
    return 1;
}

int menu_opt_XTRA(void *arg)
{
    int key = (int) arg;
    if (key >= '1' && key <= '6') {
        playsound(key-'1' + 2);
        return 1;
    } return 0;
}

menu_t menu_FL, menu_percontact;

int menu_list_FL(void *arg)
{
    msn_contact_t p;
    msn_group_t pg;
    int inBL, inAL, inFL;

    if (menu_clist(&msn.lock, &msn.FL, &p, 1, NULL) >= 0) {
        LOCK(&msn.lock);
        inBL = msn_clist_find(&msn.BL, p.login) != NULL;
        inAL = msn_clist_find(&msn.AL, p.login) != NULL;
        inFL = msn_clist_gcount(&msn.FL, p.login);
        UNLOCK(&msn.lock);
        if (play_menu(&menu_FL) == 2) switch (menu_FL.cur) {
            case 1: /* Remove */
                msn_rem(msn.nfd, nftid(), 'F', p.login, p.gid);
                if (inAL && inFL == 1) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                break;
            case 0: /* Block */
                if (inBL)
                    msg(C_ERR, "%s <%s> is already blocked\n", getnick(p.login, p.nick, Config.aliases), p.login);
                else {
                    if (inAL) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                    msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                }
                break;
            case 2: /* Unblock */
                if (!inBL)
                    msg(C_ERR, "%s <%s> is not blocked\n", getnick(p.login, p.nick, Config.aliases), p.login);
                else {
                    msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                    msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                }
                break;
            case 3: /* Rename */
                menu_change_name(p.login, p.nick);
                break;
            case 4: /* Copy */
                if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                    if (pg.gid != p.gid) {
                        msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                    } else msg(C_ERR, "Destination group must not be the same\n");
                }
                break;
            case 5: /* Move */
                if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                    if (pg.gid != p.gid) {
                        msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                        msn_rem(msn.nfd, nftid(), 'F', p.login, p.gid);
                    } else msg(C_ERR, "Destination group must not be the same\n");
                }
                break;
            case 6: /* Invite */
                do_invite(&p);
                break;
            case 7: /* Per-user settings */ {
                msn_contact_t *q;
                
                LOCK(&msn.lock);
                q = msn_clist_find(&msn.FL, p.login);
                if (q == NULL) {
                    UNLOCK(&msn.lock);
                    msg(C_ERR, "Cannot find contact -- try again\n");
                    break;
                }
                mn_setchecked(&menu_percontact.item[0], q->notify);
                mn_setchecked(&menu_percontact.item[1], q->ignored);
                UNLOCK(&msn.lock);
                
                if (play_menu(&menu_percontact) == 2) {
                    if (menu_percontact.cur == 2) { /* Accept */
                        LOCK(&msn.lock);
                        q = msn_clist_find(&msn.FL, p.login);
                        if (q == NULL) {
                            UNLOCK(&msn.lock);
                            msg(C_ERR, "Cannot find contact -- try again\n");
                            break;
                        }
                        q->notify = mn_ischecked(&menu_percontact.item[0]);
                        q->ignored = mn_ischecked(&menu_percontact.item[1]);
                        draw_lst(1);
                        UNLOCK(&msn.lock);
                    }
                    break;
                } else return 1;
            }
        }
    }
    return 2;
}

menu_t menu_RL;

int menu_list_RL(void *arg)
{
    msn_contact_t p;
    msn_group_t pg;
    int inBL, inAL;
    if (menu_clist(&msn.lock, &msn.RL, &p, 0, NULL) >= 0) {
        LOCK(&msn.lock);
        inBL = msn_clist_find(&msn.BL, p.login) != NULL;
        inAL = msn_clist_find(&msn.AL, p.login) != NULL;
        UNLOCK(&msn.lock);
        if (play_menu(&menu_RL) == 2) switch (menu_RL.cur) {
            case 0: /* Add */
                if (p.gid > 0)
                    msg(C_ERR, "<%s> is already in your Forward list\n", p.login);
                else if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                    msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                    if (!inBL && !inAL) msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                }
                break;
            case 1: /* Block */
                if (inBL)
                    msg(C_ERR, "<%s> is already blocked\n", p.login);
                else {
                    if (inAL) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                    msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                }
                break;
        }
    }
    return 2;
}

menu_t menu_AL;

int menu_list_AL(void *arg)
{
    msn_contact_t p;
    if (menu_clist(&msn.lock, &msn.AL, &p, 0, NULL) >= 0) {
        if (play_menu(&menu_AL) == 2) switch (menu_AL.cur) {
            case 0: /* Remove */
                msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                break;
            case 1: /* Block */
                msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                break;
        }
    }
    return 2;
}

menu_t menu_BL;

int menu_list_BL(void *arg)
{
    msn_contact_t p;
    if (menu_clist(&msn.lock, &msn.BL, &p, 0, NULL) >= 0) {
        if (play_menu(&menu_BL) == 2) switch (menu_BL.cur) {
            case 0: /* Remove */
                msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                break;
            case 1: /* Allow */
                msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                break;
        }
    }
    return 2;
}

menu_t menu_GL;

int menu_list_GL(void *arg)
{
    msn_group_t pg;
    if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
        if (play_menu(&menu_GL) == 2) switch (menu_GL.cur) {
            case 0: /* Remove */
                msn_rmg(msn.nfd, nftid(), pg.gid);
                break;
            case 1: /* Rename */
                menu_change_gname(&pg);
                break;
        }
    }
    return 2;
}

int menu_list_CU(void *arg)
{
    int c;
    c = msn_list_cleanup(&msn.FL, &msn.RL);
    msg(C_MSG, "FL: %d obsolete entries\n", c);
    c = msn_list_cleanup(&msn.AL, &msn.RL);
    msg(C_MSG, "AL: %d possibly obsolete entries\n", c);
    c = msn_list_cleanup(&msn.BL, &msn.RL);
    msg(C_MSG, "BL: %d possibly obsolete entries\n", c);
    return 2;
}

menu_t menu_AC;

void menu_add_contact()
{
    char con[SML];
    int inBL, inAL;
    msn_group_t pg;

    con[0] = 0;
    if (get_string(C_EBX, 0, "Account: ", con, SML)) {
        LOCK(&msn.lock);
        inBL = msn_clist_find(&msn.BL, con) != NULL;
        inAL = msn_clist_find(&msn.AL, con) != NULL;
        UNLOCK(&msn.lock);
        if (play_menu(&menu_AC) == 2) switch (menu_AC.cur) {
            case 0: /* Forward */
                if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                    msn_add(msn.nfd, nftid(), 'F', con, pg.gid);
                    if (!inBL && !inAL) msn_add(msn.nfd, nftid(), 'A', con, -1);
                }
                break;
            case 1: /* Block */
                if (inBL)
                    msg(C_ERR, "<%s> is already blocked\n", con);
                else {
                    if (inAL) msn_rem(msn.nfd, nftid(), 'A', con, -1);
                    msn_add(msn.nfd, nftid(), 'B', con, -1);
                }
                break;
        }
    }
}

void menu_add_group()
{
    char grp[SML];

    grp[0] = 0;
    if (get_string(C_EBX, 0, "Group name: ", grp, SML)) msn_adg(msn.nfd, nftid(), grp);
}

menu_t menu_Add;

int menu_list_add(void *arg)
{
    if (play_menu(&menu_Add) == 2) switch (menu_Add.cur) {
        case 0: /* Contact */
            menu_add_contact();
            return 2;
        case 1: /* Group */
            menu_add_group();
            return 2;
    }
    return 1;
}

int menu_srv_MAIL(void *arg)
{
    LOCK(&msn.lock);
    msg(C_NORMAL, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    UNLOCK(&msn.lock);
    return 2;
}

void do_ping()
{
    gettimeofday(&tv_ping, NULL);
    msn_png(msn.nfd);
}

int menu_srv_PNG(void *arg)
{
    show_ping_result++; 
    do_ping(); 
    return 2;
}

int menu_srv_RAW(void * arg)
{
    char s[SML] = {0};
    int v = 0;

    msg(C_DBG, "next tid: %u\n", _nftid);
    if (get_string(C_EBX, 0, "COMMAND: ", s, SML)) {
        strcat(s, "\r\n");
        sendstr(s);
        v = 2;
    } else v = 1;
    msg2(C_MNU, copyright_str);
    return v;
}

int menu_srv_NICK(void *arg)
{
    if (menu_change_name(msn.login, msn.nick)) return 2;
    else return 1;
}
        
menu_t menu_options, menu_server, menu_status, menu_lists, menu_main;

void menus_init()
{
    mn_init(&menu_options, 5, -1, 
            "Variables", 'v', 0, menu_opt_VAR,
            "Query", 'q', 0, menu_opt_QRY, 
            "Write", 'w', 0, menu_opt_WR,
            "Password", 'p', 0, menu_opt_PW,
            "(1)-(6) Sound test", -1, -1, menu_opt_SND, 
            menu_opt_XTRA);

    mn_init(&menu_server, 6, -1, 
            "Nickname", 'n', 0, menu_srv_NICK,
            "RL prompt", 'r', 0, menu_srv_RL,
            "All others", 'a', 0, menu_srv_AO,
            "Mailbox", 'm', 0, menu_srv_MAIL,
            "Ping", 'p', 0, menu_srv_PNG,
            "Send command", 'c', 5, menu_srv_RAW,
            NULL);
    
    mn_init(&menu_status, 9, -1, 
            "On-line", 'n', 1, menu_status_CB,
            "Idle", 'i', 0, menu_status_CB,
            "Away", 'a', 0, menu_status_CB,
            "Busy", 's', 2, menu_status_CB,
            "BRB", 'b', 0, menu_status_CB,
            "Phone", 'p', 0, menu_status_CB,
            "Lunch", 'l', 0, menu_status_CB,
            "Hidden", 'h', 0, menu_status_CB,
            "Disconnect", 'd', 0, menu_status_CB,
            NULL);
    
    mn_init(&menu_lists, 8, -1,
            "Forward", 'f', 0, menu_list_FL,
            "Reverse", 'r', 0, menu_list_RL,
            "Allow", 'a', 0, menu_list_AL,
            "Block", 'b', 0, menu_list_BL,
            "Group", 'g', 0, menu_list_GL,
            "Add", 'd', 1, menu_list_add,
            "Export aliases", 'x', 1, export_nicks,
            "Clean up", 'c', 0, menu_list_CU,
            NULL);

    mn_init(&menu_FL, 8, -1,
            "Block", 'b', 0, NULL, "Remove", 'r', 0, NULL,
            "Unblock", 'u', 0, NULL, "Rename", 'n', 2, NULL,
            "Copy", 'c', 0, NULL, "Move", 'm', 0, NULL, 
            "Invite", 'i', 0, NULL, "Per-contact settings", 'p', 0, NULL,
            NULL);

    mn_init(&menu_percontact, 4, -1,
            "[ ] Notifications", 's', 4, NULL, "[ ] Ignored", 'i', 4, NULL, 
            "Apply", 'a', 0, NULL, "Cancel", 'c', 0, NULL, NULL);
    menu_percontact.item[0].checked = 1; /* checkable */
    menu_percontact.item[1].checked = 1;

    mn_init(&menu_RL, 2, -1, 
            "Add", 'a', 0, NULL, "Block", 'b', 0, NULL, NULL);
    mn_init(&menu_AL, 2, -1, 
            "Remove", 'r', 0, NULL, "Block", 'b', 0, NULL, NULL);
    mn_init(&menu_BL, 2, -1, 
            "Remove", 'r', 0, NULL, "Allow", 'a', 0, NULL, NULL);
    mn_init(&menu_GL, 2, -1, 
            "Remove", 'r', 0, NULL, "Rename", 'n', 2, NULL, NULL);
    
    mn_init(&menu_RLp, 3, -1,
            "Always", 'a', 0, NULL, "Never", 'n', 0, 
            NULL, "Query", 'q', 0, NULL, NULL);
    mn_init(&menu_AO, 3, -1,
            "Allow", 'a', 0, NULL, "Block", 'b', 0, 
            NULL, "Query", 'q', 0, NULL, NULL);

    mn_init(&menu_AC, 2, -1,
            "Forward", 'f', 0, NULL, "Block", 'b', 0, NULL, NULL);
    mn_init(&menu_Add, 2, -1,
            "Contact", 'c', 0, NULL, "Group", 'g', 0, NULL, NULL);

    mn_init(&menu_YN, 2, -1, "Yes", 'y', 0, NULL, "No", 'n', 0, NULL, NULL);
    menu_YN.attr = attrs[C_EBX];
    
    mn_init(&menu_main, 8, -1,
            "Connect", 'c', 0, NULL,
            "Status", 's', 0, NULL,
            "Lists", 'l', 0, NULL,
            "Server", 'v', 3, NULL,
            "Options", 'o', 0, NULL,
            "Invite", 'i', 0, NULL,
            "Note", 't', 2, NULL,
            "Quit", 'q', 0, NULL,
            NULL);
}

int log_in(int startup)
{
    int r;
    if (msn.nfd != -1) {
        msg(C_ERR, "You are already logged in\n");
        return 0;
    }
    if (startup) {
        if (!msn.login[0]) return 0;
    } else {
        if (!(r = get_string(C_EBX, 0, "Login as: ", msn.login, SML))) return 0;
        if (r == 1) {
            if (strchr(msn.login, '@') == NULL) strcat(msn.login, "@hotmail.com");
            Strcpy(msn.nick, msn.login, SML);
            msn.pass[0] = 0;
            draw_status(1);
        }
    }
    if (startup) {
        if (!msn.pass[0]) return 0;
    } else if (!get_string(C_EBX, 1, "Password: ", msn.pass, SML)) return 0;
    msg2(C_MNU, "Logging in ...");
    
    while ((r = do_login()) == 1);
    if (r == 0) return 0;
    msn.nfd = -1;
    
    msg(C_ERR, "Failed to connect to server\n");
    return -1;
}

int do_update_nicks()
{
    int c = 0;
    if (Config.update_nicks) {
        msn_contact_t *p;
        for (p = msn.FL.head; p != NULL; p = p->next)
        if ((p->dirty && (Config.update_nicks == 2)) || 
            ((Config.update_nicks == 1) && (strstr(p->nick, p->login) == NULL))) {
                msg(C_DBG, "Renaming <%s> to %s\n", p->login, p->nick);
                msn_rea(msn.nfd, nftid(), p->login, p->nick);
                c++;
        }
    }
    return c;
}

/* per-user settings */

int skip_pcs = 0;

void write_pcs() 
{
    msn_contact_t *p;
    FILE *f;
    char fn[SML];

    if (skip_pcs) return;
    sprintf(fn, "%s/%s/per_user", Config.cfgdir, msn.login);
    f = fopen(fn, "w");
    fprintf(f, "#GTMESS:%s:PCS\n", VERSION);
    if (f != NULL) {
        for (p = msn.FL.head; p != NULL; p = p->next )
            if (p->notify != 1 || p->ignored == 1)
                fprintf(f, "%s\t%d\t%d\n", p->login, p->notify, p->ignored);
        fclose(f);
    }
}

void log_out(int panic)
{
    int retcode = 0;
    struct timespec timeout;
            

    sboard_leave_all(panic);
    if (msn.nfd == -1) return;
    if (panic == 0) {
        write_pcs();
        retcode = do_update_nicks();
        timeout = nowplus(5+retcode/2);
        msg(C_NORMAL, "Logging out...\n");
        LOCK(&msn.lock);
    }
    msn_out(msn.nfd);
    if (panic == 0) {
        timeout = nowplus(5);
        retcode = pthread_cond_timedwait(&cond_out, &msn.lock, &timeout);
        UNLOCK(&msn.lock);
    } else sleep(1);
    if (!panic && retcode != 0 && msn.nfd != -1) {
        pthread_cancel(msn.thrid);
        pthread_join(msn.thrid, NULL);
    }
}

void set_flskip(int delta)
{
    int total;
    msn_contact_t *p;
    LOCK(&msn.lock);
    total = msn.GL.count;
    if (Config.online_only == 0) total += msn.FL.count;
    else 
        for (p = msn.FL.head; p != NULL; p = p->next)
            if (p->status >= MS_NLN) total++;
    msn.flskip += delta;
    if (msn.flskip < -total) msn.flskip = -total;
    if (msn.flskip > total) msn.flskip = total;
    draw_lst(1);
    UNLOCK(&msn.lock);
}

void set_plskip(int delta)
{
    msn_sboard_t *s;
    int total;
    LOCK(&SX);
    s = SL.head;
    if (s != NULL) { 
        total = s->PL.count + 1;
        s->plskip += delta;
        if (s->plskip < -total) s->plskip = -total;
        if (s->plskip > total) s->plskip = total;
        draw_plst(s, 1);
    }
    UNLOCK(&SX);
}

void set_msgskip(int delta)
{
    LOCK(&w_msg.lock);
    w_msg_top += delta;
    if (w_msg_top < 0) w_msg_top = 0;
    if (w_msg_top + w_msg.h > W_MSG_LBUF) w_msg_top = W_MSG_LBUF - w_msg.h;
    UNLOCK(&w_msg.lock);
    draw_msg(1);
}

void set_dlgskip(int delta)
{
    msn_sboard_t *s;
    LOCK(&SX);
    s = SL.head;
    if (s != NULL) {
        LOCK(&s->w_dlg.lock);

        s->w_dlg_top += delta;
        if (s->w_dlg_top < 0) s->w_dlg_top = 0;
        if (s->w_dlg_top + s->w_dlg.h > W_DLG_LBUF) s->w_dlg_top = W_DLG_LBUF - s->w_dlg.h;
        UNLOCK(&s->w_dlg.lock);
    }
    draw_dlg(s, 1);
    UNLOCK(&SX);
}


void sboard_new()
{
    char s[SML];

    sprintf(s, "XFR %u SB\r\n", nftid());
    /* enqueue the request; normally the server will respond in
       the same order so we do not have to do any request tracking */
    queue_put(&q_sb_req, 0, NULL, 0);
    Write(msn.nfd, s, strlen(s));
}

void sb_keydown(int c)
{
    switch (c) {
        case 14: sboard_new(); break; /* ^N */
        case 23: sboard_kill(); break; /* ^W */
        case 24: sboard_leave(); break; /* ^X */
        case KEY_F(2): sboard_next(); break;
        case KEY_F(1): sboard_prev(); break;
        case KEY_F(3): sboard_next_pending(); break;
        case KEY_PPAGE: set_dlgskip(-1); break;
        case KEY_NPAGE: set_dlgskip(1); break;
        case -'&':
        case -(KEY_F(7)): set_plskip(-1); break;
        case -'*':
        case -(KEY_F(8)): set_plskip(1); break;
        case '\r': if (!sb_type('\r')) sb_blah(); break;
        default: if (c > 0) sb_type(c);
    }            
}

void load_aliases()
{
    FILE *f;
    char s[SML], fn[SML];
    char ulogin[SML], unick[SML], alogin[SML], anick[SML];
    int c = 0;

    sprintf(fn, "%s/aliases", Config.cfgdir);
    if ((f = fopen(fn, "r")) != NULL) {
        while (fgets(s, SML, f) != NULL) {
            if (s[0] != '#') {
                if (sscanf(s, "%s %s", ulogin, unick) == 2) {
                    url2str(ulogin, alogin);
                    url2str(unick, anick);
                    hash_tbl_update(&Aliases, alogin, anick);
                    c++;
                } /* 2 fields */
            } /* not comment */
        } /* not eof */
        msg(C_DBG, "Loaded %d alias%s\n", c, (c!=1)?"es": "");
        fclose(f);
    } /* file exists */
}

void setup_cfg_dir()
{
    char tmp[SML];
    
    get_home_dir(Config.cfgdir, SCL);
    strcat(Config.cfgdir, "/.gtmess");
    
    if (!file_exists(Config.cfgdir)) {
        mkdir(Config.cfgdir, 0700);
        sprintf(tmp, "%s/received", Config.cfgdir);
        mkdir(tmp, 0700);
        sprintf(tmp, "%s/notify.pip", Config.cfgdir);
        mkfifo(tmp, 0600);
    }
}

int main(int argc, char **argv)
{
    int c, paste, escape, mstatus;
    
    sprintf(copyright_str, "gtmess, version %s (%s), (c) 2002-2007 by George M. Tzoumas", VERSION, VDATE);
    printf("%s\n", copyright_str);
    
    queue_init(&msg_q);
    setup_cfg_dir();
    
    msn_ic[0] = msn_ic[1] = (iconv_t) -1;
    
    srand48(time(NULL));

    OConfigTbl = (struct cfg_entry *) Malloc(sizeof(ConfigTbl));
    memcpy(OConfigTbl, ConfigTbl, sizeof(ConfigTbl));
    parse_config(argc, argv);
    msn.nfd = -1;
    config_init();
    
    signal(SIGINT, gtmess_exit_panic);
    signal(SIGQUIT, gtmess_exit_panic);
    signal(SIGTERM, gtmess_exit_panic);
    
    pthread_mutex_init(&nftid_lock, NULL);
    pthread_mutex_init(&time_lock, NULL);
    calc_time();
   
    pthread_mutex_init(&msn.lock, NULL);
    msn_init(&msn);
    pthread_mutex_init(&SX, NULL);
    SL.head = SL.start = NULL; SL.count = 0;
    SL.out_count = 0;
    pthread_cond_init(&SL.out_cond, NULL);
    
    XL.head = XL.tail = XL.cur = NULL; XL.count = 0;
    
    hash_tbl_init(&Aliases);

    screen_init(Config.colors);
    menus_init();

    pthread_key_create(&key_sboard, NULL);
    pthread_setspecific(key_sboard, NULL);
    
    q_sb_req.head = q_sb_req.tail = NULL;
    q_sb_req.count = 0;
    
    pass_init();
    unotif_init(Config.sound, Config.popup);
    
    interval_init();
    msnftp_init();
    
    LOCK(&msn.lock);
    LOCK(&SX);
    draw_all();
    UNLOCK(&SX);
    UNLOCK(&msn.lock);

    msg(C_DBG, 
        "gtmess version %s (%s)\n"
        "MSN Messenger Client for UNIX Console\n"
        "(c) 2002-2006 by George M. Tzoumas\n"
        "gtmess is free software, covered by the\nGNU General Public License.\n"
        "There is absolutely no warranty for gtmess.\n\n", VERSION, VDATE);

    load_aliases();

    if (Config.auto_login) log_in(1);
    
    msg2(C_MNU, "Welcome to gtmess. Press F9 for menu.");
    
    paste = 0;
    escape = 0;
    ui_active = 1;
    
    while (1) {
        if (paste == 0) c = getch(); else paste = 0;
        time(&keyb_time);
        if (!iamback && msn.status == MS_IDL) {
            if (msn.nfd != -1) msn_chg(msn.nfd, nftid(), MS_NLN);
            iamback = 1;
        }
        if (c == KEY_F(10) && escape == 0) break;
        switch (c) {
            case KEY_F(4): 
                wvis ^= 1; 
                LOCK(&SX);
                draw_sb(SL.head, 1);
                UNLOCK(&SX);
                break; 
            case KEY_F(5): set_msgskip(-1); break;
            case KEY_F(6): set_msgskip(1); break;
            case KEY_F(7): set_flskip(-1); break;
            case KEY_F(8): set_flskip(1); break;
            case 7: beep(); break; /* ^G */
            case 12: redraw_screen(0); break; /* ^L */
            case 6: /* ^F */
                    Config.online_only = ConfigTbl[13].ival = !Config.online_only;
                    redraw_screen(0);
                    break;
            case KEY_RESIZE: 
                redraw_screen(1); 
                if (escape) {
                    menu_main.cur = menu_main.left = 0;
                    LOCK(&scr_lock);
                    mn_draw(&menu_main, SLINES-1, 0, stdscr);
                    UNLOCK(&scr_lock);
                }
                break;
            case KEY_F(9):
            case 27: /* escape mode */
                escape ^= 1;
                break;
            default:
                if (escape) {
                    if (c == KEY_BACKSPACE || c == '-') {
                        escape = 0;
                        break;
                    } else if (c == '0') {
                        c = KEY_F(10);
                        paste = 1;
                        escape = 0;
                        break;
                    } else if (c >= '1' && c <= '9') {
                        int k = c - '0';
                        c = KEY_F(k);
                        paste = 1;
                        escape = 0;
                        break;
                    } else if (c == '/') {
                        char s[SML] = {0};

                        if (get_string(C_EBX, 0, "/", s, SML)) {
                            msg(C_ERR, "Invalid command\n");;
                        }
                        escape = 0;
                        break;
                    } else if ((mstatus = mn_keydown(&menu_main, c)) == 2) {
                        char note[SML] = {0};

                        switch (menu_main.cur) {
                        case 0: /* Connect */
                            log_in(0);
                            LOCK(&SX);
                            draw_sb(SL.head, 0);
                            UNLOCK(&SX);
                            escape = 0;
                            break;
                        case 7: /* Quit */
                            c = KEY_F(10);
                            paste = 1;
                            escape = 0;
                            break;
                        case 6: /* Note */
                            if (get_string(C_EBX, 0, "Note:", note, SML) && note[0]) 
                                msg(C_NORMAL, "%s\n", note);
                            escape = 0;
                            break;
                        case 4: /* Options */
                            if (play_menu(&menu_options) == 2) escape = 0;
                            break;
                        case 1: /* Status */
                        case 2: /* Lists */
                        case 3: /* Server */
                        case 5: /* Invite */
                            if (msn.nfd == -1) {
                                msg(C_ERR, "You must be logged in for this option to work\n");
                            } else switch (menu_main.cur) {
                   /* Status */ case 1: if (play_menu(&menu_status) == 2) escape = 0; break;
                   /* Lists */  case 2: if (play_menu(&menu_lists) == 2) escape = 0; break;
                   /* Server */ case 3: if (play_menu(&menu_server) == 2) escape = 0; break;
                   /* Invite */ case 5: if (menu_invite()) escape = 0; break;
                            }
                            break;
                        }
                    } else if (wvis == 0) sb_keydown(-c); else xf_keydown(-c);
                } else {
                    if (wvis == 0) sb_keydown(c); else xf_keydown(c);
                }
        } /* switch */
        if (escape == 0) msg2(C_MNU, copyright_str);
        else {
            LOCK(&scr_lock);
            mn_draw(&menu_main, SLINES-1, 0, stdscr);
            UNLOCK(&scr_lock);
        }        
    } /* while */
    _gtmess_exit(0);
    return 0;
}
