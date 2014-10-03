/*
 *    gtmess.c
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

/* 

TODO LIST
=========

DELETE EVERYTHING AND WRITE THE CLIENT FROM SCRATCH USING C++ :-)


planned for next version 0.91 or 0.92
-------------------------------------
FLN: contact_rename = 0: never, 1: when nick < email, 2:always
allow mult-line input or \xx sequences
full console (sb & notification) logging
advertise (don't freak, it's not adware, you'll just be able
to tell your friends where to get this cool client from! :-))
gtmess-notify: write manpage
gtmess-gw command-line args to change default net addresses
reconnect to sb even if connection has been closed

MAYBE in SOME future version
----------------------------
use MSNP9 ClientIP info
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
auto PNG QNG (keep connection alive!)
use iconv() at invitation messages, too!
phone numbers
add global ui command line

if I ever have TOO MUCH FREE TIME 
and there are no other features to add :-)
--------------------------------------------------
fix issue with multiple file accepts (after cancel!)
fix issue with number of threads spawned, at msnftpd!
filesize limit is 2GB!!!
should we rely on good luck (for lrand48) ?)
do not always assume UTF-8 encoding on server
prove that cleanup handler restores locks :-)
list menu should support selections (multiple users)
group block/unblock
fix issue whether type notif is sent during history browsing
histlen
*/

#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<pthread.h>
#include<signal.h>
#include<string.h>

#include<sys/time.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/stat.h>

#include<netdb.h>
#include<sys/socket.h>

#include<curses.h>
#include<stdarg.h>
#include<errno.h>
#include<ctype.h>
#include<time.h>

#include<iconv.h>
#include<locale.h>

#include"../config.h"

#include"../inty/inty.h"
#include"md5.h"

#include"nserver.h"
#include"screen.h"
#include"editbox.h"
#include"msn.h"
#include"pass.h"
#include"sboard.h"
#include"unotif.h"
#include"xfer.h"
#include"hash_cfg.h"
#include"hash_tbl.h"

#include"gtmess.h"

static struct cfg_entry ConfigTbl[] =
  {
    {"log_traffic", "", 0, 1, 0, 1},
    {"colors", "", 0, 1, 0, 2},
    {"sound", "", 1, 1, 0, 2},
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
    {"msg_debug", "", 0, 1, 0, 1},
    {"msg_notify", "", 1, 1, 0, 7200},
    {"idle_sec", "", 180, 1, 0, 7200},
    {"auto_login", "", 1, 1, 0, 1},
    {"invitable", "", 1, 1, 0, 1},
    {"gtmesscontrol_ignore", "", 0, 0, 0, 0},
    {"max_nick_len", "", 0, 1, 0, 1024}
  };

struct cfg_entry *OConfigTbl;
config_t Config;

int NumConfigVars = 0;

/*extern syn_hdr_t syn_cache;
extern FILE *syn_cache_fp;*/
char *ZS = "";
char MyIP[SML];
char copyright_str[80];
int utf8_mode;

hash_table_t Aliases; /* alias list */

pthread_cond_t cond_out = PTHREAD_COND_INITIALIZER;
struct timeval tv_ping;

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

int SCOLS, SLINES;
extern char msg2_str[SML];
extern cattr_t msg2_attr;


void log_out(int panic);
void sboard_leave_all(int panic);

unsigned int _nftid = 1;
pthread_mutex_t nftid_lock;

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
    if (Config.log_traffic) fclose(flog);
    
    log_out(panic);
    pthread_cancel(ithrid);
    pass_done();
    unotif_done();
    if (msn_ic[0] != (iconv_t) -1) iconv_close(msn_ic[0]);
    if (msn_ic[1] != (iconv_t) -1) iconv_close(msn_ic[1]);
    usleep(500000);
    endwin();
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
    
    pthread_mutex_lock(&i_draw_lst_lock);
    newt = time(NULL) + after;
    if (i_draw_lst_handled) {
        i_draw_lst_handled = 0;
        i_draw_lst_time_start = newt;
        i_draw_lst_time_end = newt;
    } else if (newt > i_draw_lst_time_end) i_draw_lst_time_end = newt;
    pthread_mutex_unlock(&i_draw_lst_lock);
}

void *interval_daemon(void *dummy)
{
    time_t now;
    while (1) {
        pthread_testcancel();
        sleep(1);
        pthread_testcancel();
        
        calc_time();
        
        pthread_mutex_lock(&i_draw_lst_lock);
        time(&now);
        if (!i_draw_lst_handled) {
            if (now >= i_draw_lst_time_start) draw_lst(1);
            if (now >= i_draw_lst_time_end) i_draw_lst_handled = 1;
        }
        pthread_mutex_unlock(&i_draw_lst_lock);
        
        draw_time(1);
        
        if (msn.nfd != -1 && Config.idle_sec && msn.status == MS_NLN &&
            Config.idle_sec + keyb_time < now) {
                msn_chg(msn.nfd, nftid(), MS_IDL);
                keyb_time = now;
                iamback = 0;
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

char *getnick(char *login, char *nick)
{
    char *alias;
    int maxlen;
    
    if (login == NULL || Config.aliases == 0) return nick;
    if ((alias = hash_tbl_find(&Aliases, login)) != NULL) {
        maxlen = Config.max_nick_len;
        if (maxlen == 0) maxlen = w_lst.w-1;
        if (Config.aliases == 2 || 
            strstr(nick, login) != NULL || strlen(nick) > maxlen) return alias;
        else return nick;
    } else {
        return nick;
    }
}

int Write(int fd, void *buf, size_t count)
{
    int r;

    r = write(fd, buf, count);
    if (r < 0) msg(C_ERR, "write(): %s\n", strerror(errno));
    return r;
}

void *Malloc(size_t size)
{
    void *p;

    if ((p = malloc(size)) == NULL) panic("malloc(): out of memory");
    return p;
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
            /* bug: multiple same warnings, when user on multiple groups! *
            msg(C_MSG, "%s <%s> has removed you from his/her contact list\n",
                getnick(p->login, p->nick), p->login);
    /* update FL with IL *
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
/*    char udir[SML];*/

    msg(C_NORMAL, "Connecting to server %s ...\n", msn.notaddr);
    msn.nfd = do_connect(msn.notaddr);
    if (msn.nfd < 0) return -1;
    GetMyIP(msn.nfd, MyIP);

    if ((r = msn_login_init(msn.nfd, nftidn(3), msn.login, Config.cvr, hash)) == 1) {
        strcpy(msn.notaddr, hash);
        msg(C_MSG, "Notification server is %s\n", msn.notaddr);
        return 1;
    }
                
    if (r != 0) {
        msg(C_ERR, "%s\n", msn_ios_str(r));
        return -3;
    }

    if (get_login_server(dalogin) != NULL) {
        if ((r = get_ticket(dalogin, msn.login, msn.pass, hash, ticket)) == 0) {
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
/*    sprintf(udir, "%s/%s", Config.cfgdir, msn.login);
    mkdir(udir, 0700);*/
    
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
      erase();
      refresh();
/*      redrawwin(curscr);*/
      if (resize) screen_resize();
      draw_all(); 
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
                    "aliases=<n>\t\tuse contact aliases [1], 0=disabled, 1=smart, 2=always\n"
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
                    "login=<s>\t\tdefault login account\n"
                    "log_traffic=<b>\t\tlog network traffic [0]\n"
                    "max_nick_len=<n>\tmax nickname length before use of alias [0], 0=fit\n"
                    "msg_debug=<n>\t\thandle unknown msgs [0], 0 = ignore,\n"
                    "\t\t\t1 = show type, 2 = show whole msg\n"
                    "msg_notify=<n>\t\textra notification on received messages [1],\n"
                    "\t\t\t0=never, 1=when idle, or when <n> seconds have passed\n"
                    "msnftpd=<n>\t\tport of msnftp server [6891], 0=disable\n"
                    "online_only=<b>\t\tshow only online contacts [0]\n"
                    "password=<s>\t\tdefault password\n"
                    "popup=<b>\t\tenable/disable popup notification window [1]\n"
                    "server=<s>[:<n>]\tdispatch/notification server (optional port)\n"
                    "syn_cache=<b>\t\tcache contact/group lists [0]\n"
                    "sound=<n>\t\tsound mode [1]\n"
                    "\t\t\t<n> = 0 for no sound, 1 for beep, 2 for sound effects\n"
                    "time_user_types=<n>\tinterval of typing notifications [%d]\n"
                    "\n"
                    "EXAMPLE:\n"
                    "\t%s -Ologin=george -Opassword=secret123 -Oinitial_status=1\n",
             argv0, ConfigTbl[4].ival, argv0);
}

int export_nicks()
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
        msg2(C_MNU, "(Y)es  (N)o");
        while (1) {
            c = tolower(getch());
            if (c == KEY_RESIZE) redraw_screen(1);
            if (c == 'n') {
                msg(C_ERR, "Alias export cancelled\n");
                return -1;
            }
            if (c == 'y' || c == 'a') break;
            beep();
        }
        msg2(C_MNU, copyright_str);
    }
    f = fopen(fn, "w");
    if (f == NULL) {
        msg(C_ERR, "Failed to create output file\n");
        return -2;
    }
    
    for (p = msn.FL.head, c = 0; p != NULL; p = p->next) {
/*        str2url(p->login, ulogin);
        str2url(p->nick, unick);*/
        fprintf(f, "%s %s\n", p->login, p->nick);
        c++;
    }
    fclose(f);
    msg(C_DBG, "Exported %d alias%s\n", c, (c>1)?"es": "");
    
    return 0;
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
                strncpy(e->sval, eq+1, 255);
                e->sval[255] = 0;
                return 0;
            } else { /* integer */
                if (sscanf(eq+1, "%d", &ival) == 1) {
                    if (ival < e->min) {
                        msg(C_ERR, "parse_cfg_entry(): value too small, %d assumed\n", e->min);
                        ival = e->min;
                    } else if (ival > e->max) {
                        msg(C_ERR, "parse_cfg_entry(): value too big, %d assumed\n", e->max);
                        ival = e->max;
                    }
                    e->ival = ival;
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

void get_lc_ctype(char *dest)
{
    char loc[SML], *s;
    
    s = setlocale(LC_CTYPE, "");
    if (s != NULL) {
        strcpy(loc, s);
        s = strchr(loc, '@');
        if (s != NULL) *s = 0;
        s = strchr(loc, '.');
        if (s != NULL) {
            strcpy(dest, s+1);
            return;
        }
    }
    strcpy(dest, "ISO8859-1");
}

void get_home_dir(char *dest)
{
    char *s;
    if ((s = getenv("HOME")) != NULL)
        strcpy(dest, s);
    else strcpy(dest, ".");
}

void config_init()
{
    int c = 0;
    
    Config.log_traffic = ConfigTbl[c++].ival;
    Config.colors = ConfigTbl[c++].ival;
    Config.sound = ConfigTbl[c++].ival;
    Config.popup = ConfigTbl[c++].ival;
    Config.time_user_types = ConfigTbl[c++].ival;
    strcpy(Config.cvr, ConfigTbl[c++].sval);
    Config.cert_prompt = ConfigTbl[c++].ival;
    Config.common_name_prompt = ConfigTbl[c++].ival;
    strcpy(Config.console_encoding, ConfigTbl[c++].sval);
    strcpy(Config.server, ConfigTbl[c++].sval);
    strcpy(Config.login, ConfigTbl[c++].sval);
    strcpy(Config.password, ConfigTbl[c++].sval);
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
    strcpy(Config.gtmesscontrol_ignore, ConfigTbl[c++].sval);
    Config.max_nick_len = ConfigTbl[c++].ival;
    NumConfigVars = c;
    
    sprintf(Config.datadir, "%s", DATADIR);
    strcpy(Config.ca_list, "./root.pem");
    if (!file_exists(Config.ca_list)) {
        sprintf(Config.ca_list, "%s/root.pem", Config.cfgdir);
        if (!file_exists(Config.ca_list)) 
            sprintf(Config.ca_list, "%s/root.pem", Config.datadir);
    }

    if (msn.nfd == -1) {
        if (Config.login[0] && strchr(Config.login, '@') == NULL) strcat(Config.login, "@hotmail.com");
        strcpy(msn.notaddr, Config.server);
        strcpy(msn.login, Config.login);
        strcpy(msn.pass, Config.password);
    }
    
    if (msn_ic[0] != (iconv_t) -1) iconv_close(msn_ic[0]);
    if (msn_ic[1] != (iconv_t) -1) iconv_close(msn_ic[1]);
    
    if (strcmp(Config.console_encoding, "*locale*") == 0)
        get_lc_ctype(Config.console_encoding);

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

void menu_change_status()
{
    msg2(C_MNU, "O(n)-line (I)dle (A)way Bu(s)y (B)RB (P)hone (L)unch (H)idden");
    switch (tolower(getch())) {
        case 'n': msn_chg(msn.nfd, nftid(), MS_NLN); break;
        case 'i': msn_chg(msn.nfd, nftid(), MS_IDL); break;
        case 'a': msn_chg(msn.nfd, nftid(), MS_AWY); break;
        case 's': msn_chg(msn.nfd, nftid(), MS_BSY); break;
        case 'b': msn_chg(msn.nfd, nftid(), MS_BRB); break;
        case 'p': msn_chg(msn.nfd, nftid(), MS_PHN); break;
        case 'l': msn_chg(msn.nfd, nftid(), MS_LUN); break;
        case 'h': msn_chg(msn.nfd, nftid(), MS_HDN); break;
        case KEY_RESIZE: redraw_screen(1); break;
    }
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
                    a[i]->gid, i+1, count, getnick(a[i]->login, a[i]->nick), a[i]->login);
        else
            msg2(C_MNU, "(%d/%d) %s <%s>", i+1, count, getnick(a[i]->login, a[i]->nick), a[i]->login);
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
                    msg(C_NORMAL, "%2d. [%d] %s <%s>\n", j + 1, a[j]->gid, getnick(a[j]->login, a[j]->nick), a[j]->login);
                else for (j = 0; j < count; j++)
                    msg(C_NORMAL, "%2d. %s <%s>\n", j + 1, getnick(a[j]->login, a[j]->nick), a[j]->login);
                break;
            case 'q':
                msg(C_NORMAL, "NICK: %s\n", a[i]->nick);
                msg(C_NORMAL, "ALIAS: %s\n", getnick(a[i]->login, a[i]->nick));
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

    strcpy(newn, q->name);
    if (get_string(C_EBX, 0, "New name: ", newn))
        msn_reg(msn.nfd, nftid(), q->gid, newn);
}

void menu_change_name(char *login, char *old)
{
    char newn[SML];

    strcpy(newn, old);
    if (get_string(C_EBX, 0, "New name: ", newn))
        msn_rea(msn.nfd, nftid(), login, newn);
}

void menu_invite()
{
    msn_contact_t p;
    if (menu_clist(&msn.lock, &msn.FL, &p, 1, cfilt_online) >= 0) {
        LOCK(&SX);
        if (SL.head == NULL || !SL.head->cantype) {
            UNLOCK(&SX);
            msg(C_ERR, "Not in a switchboard session\n");
        } else {
            msn_cal(SL.head->sfd, SL.head->tid++, p.login);
            UNLOCK(&SX);
        }
    }
}

void menu_lists()
{
    msn_contact_t p;
    msn_group_t pg;
    int inBL, inAL, inFL;

    msg2(C_MNU, "(F)orward  (R)everse  (A)llow  (B)lock  (G)roup  E(x)port aliases");
    switch (tolower(getch())) {
        case KEY_RESIZE: redraw_screen(1); break;
        case 'f':
            if (menu_clist(&msn.lock, &msn.FL, &p, 1, NULL) >= 0) {
                LOCK(&msn.lock);
                inBL = msn_clist_find(&msn.BL, p.login) != NULL;
                inAL = msn_clist_find(&msn.AL, p.login) != NULL;
                inFL = msn_clist_gcount(&msn.FL, p.login);
                UNLOCK(&msn.lock);
                msg2(C_MNU, "(B)lock (R)emove (U)nblock Re(n)ame (C)opy (M)ove (I)nvite");
                switch (tolower(getch())) {
                    case KEY_RESIZE: redraw_screen(1); break;
                    case 'r':
                        msn_rem(msn.nfd, nftid(), 'F', p.login, p.gid);
                        if (inAL && inFL == 1) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                        break;
                    case 'b':
                        if (inBL)
                            msg(C_ERR, "%s <%s> is already blocked\n", getnick(p.login, p.nick), p.login);
                        else {
                            if (inAL) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                            msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                        }
                        break;
                    case 'u':
                        if (!inBL)
                            msg(C_ERR, "%s <%s> is not blocked\n", getnick(p.login, p.nick), p.login);
                        else {
                            msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                            msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                        }
                        break;
                    case 'n':
                        menu_change_name(p.login, p.nick);
                        break;
                    case 'c':
                        if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                            if (pg.gid != p.gid) {
                                msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                            } else msg(C_ERR, "Destination group must not be the same\n");
                        }
                        break;
                    case 'm':
                        if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                            if (pg.gid != p.gid) {
                                msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                                msn_rem(msn.nfd, nftid(), 'F', p.login, p.gid);
                            } else msg(C_ERR, "Destination group must not be the same\n");
                        }
                        break;
                    case 'i':
                        LOCK(&SX);
                        if (SL.head == NULL || !SL.head->cantype) {
                            UNLOCK(&SX);
                            msg(C_ERR, "Not in a switchboard session\n");
                        } else {
                            msn_cal(SL.head->sfd, SL.head->tid++, p.login);
                            UNLOCK(&SX);
                        }
                        break;
                }
            }
            break;
        case 'r':
            if (menu_clist(&msn.lock, &msn.RL, &p, 0, NULL) >= 0) {
                LOCK(&msn.lock);
                inBL = msn_clist_find(&msn.BL, p.login) != NULL;
                inAL = msn_clist_find(&msn.AL, p.login) != NULL;
                UNLOCK(&msn.lock);
                msg2(C_MNU, "(A)dd  (B)lock");
                switch (tolower(getch())) {
                    case KEY_RESIZE: redraw_screen(1); break;
                    case 'a':
                        if (p.gid > 0)
                            msg(C_ERR, "<%s> is already in your Forward list\n", p.login);
                        else if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                            msn_add(msn.nfd, nftid(), 'F', p.login, pg.gid);
                            if (!inBL && !inAL) msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                        }
                        break;
                    case 'b':
                        if (inBL)
                            msg(C_ERR, "<%s> is already blocked\n", p.login);
                        else {
                            if (inAL) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                            msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                        }
                        break;
                }
            }
            break;
        case 'a':
            if (menu_clist(&msn.lock, &msn.AL, &p, 0, NULL) >= 0) {
                msg2(C_MNU, "(R)emove  (B)lock");
                switch (tolower(getch())) {
                    case KEY_RESIZE: redraw_screen(1); break;
                    case 'r':
                        msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                        break;
                    case 'b':
                        msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                        msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                        break;
                }
            }
            break;
        case 'b':
            if (menu_clist(&msn.lock, &msn.BL, &p, 0, NULL) >= 0) {
                msg2(C_MNU, "(R)emove  (A)llow");
                switch (tolower(getch())) {
                    case KEY_RESIZE: redraw_screen(1); break;
                    case 'r':
                        msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                        break;
                    case 'a':
                        msn_rem(msn.nfd, nftid(), 'B', p.login, -1);
                        msn_add(msn.nfd, nftid(), 'A', p.login, -1);
                        break;
                }
            }
            break;
        case 'g':
            if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                msg2(C_MNU, "(R)emove  Re(n)ame");
                switch (tolower(getch())) {
                    case KEY_RESIZE: redraw_screen(1); break;
                    case 'r':
                        msn_rmg(msn.nfd, nftid(), pg.gid);
                        break;
                    case 'n':
                        menu_change_gname(&pg);
                        break;
                }
            }
            break;
        case 'x':
            export_nicks();
            break;
    }
}

void menu_msn_options()
{
    int c, i;
    char optline[SML], tmp[SML];
    FILE *cfg_fp;

    msg2(C_MNU, "(R)L prompt  (A)ll others  (V)ar  (Q)uery  (W)rite");
    switch (tolower(getch())) {
        case KEY_RESIZE: redraw_screen(1); break;
        case 'r':
            if (msn.nfd == -1) {
                msg(C_ERR, "You are not logged in\n");
                break;
            }
            msg2(C_MNU, "(A)lways  (N)ever  (Q)uery");
            c = toupper(getch());
            if (c == KEY_RESIZE) redraw_screen(1);
            if (c == 'A' || c == 'N') msn_gtc(msn.nfd, nftid(), (char) c);
            if (c == 'Q')
                msg(C_NORMAL, "Prompt when other users add you: %s\n", msn.GTC == 'N'? "NEVER": "ALWAYS");
            break;
        case 'a':
            if (msn.nfd == -1) {
                msg(C_ERR, "You are not logged in\n");
                break;
            }
            msg2(C_MNU, "(A)llow  (B)lock  (Q)uery");
            c = toupper(getch());
            if (c == KEY_RESIZE) redraw_screen(1);
            if (c == 'A' || c == 'B') msn_blp(msn.nfd, nftid(), (char) c);
            if (c == 'Q')
                msg(C_NORMAL, "All other users: %s\n", msn.BLP == 'B'? "BLOCKED": "ALLOWED");
            break;
        case 'v':
            optline[0] = 0;
            if (get_string(C_EBX, 0, "<var>=<value>:", optline)) {
                parse_cfg_entry(optline);
                config_init();
                redraw_screen(0);
            }
            break;
        case 'q':
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
            break;
        case 'w':
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
            break;
    }
}

void menu_add_contact()
{
    char con[SML];
    int inBL, inAL;
    msn_group_t pg;

    con[0] = 0;
    if (get_string(C_EBX, 0, "Account: ", con)) {
        LOCK(&msn.lock);
        inBL = msn_clist_find(&msn.BL, con) != NULL;
        inAL = msn_clist_find(&msn.AL, con) != NULL;
        UNLOCK(&msn.lock);
        msg2(C_MNU, "(F)orward  (B)lock");
        switch (tolower(getch())) {
            case KEY_RESIZE: redraw_screen(1); break;
            case 'f':
                if (menu_glist(&msn.lock, &msn.GL, &pg) >= 0) {
                    msn_add(msn.nfd, nftid(), 'F', con, pg.gid);
                    if (!inBL && !inAL) msn_add(msn.nfd, nftid(), 'A', con, -1);
                }
                break;
            case 'b':
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
    if (get_string(C_EBX, 0, "Group name: ", grp)) msn_adg(msn.nfd, nftid(), grp);
}

void menu_add()
{
    msg2(C_MNU, "(C)ontact  (G)roup");
    switch (tolower(getch())) {
        case KEY_RESIZE: redraw_screen(1); break;
        case 'c':
            menu_add_contact();
            break;
        case 'g':
            menu_add_group();
    }
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
        if (!(r = get_string(C_EBX, 0, "Login as: ", msn.login))) return 0;
        if (r == 1) {
            if (strchr(msn.login, '@') == NULL) strcat(msn.login, "@hotmail.com");
            strcpy(msn.nick, msn.login);
            msn.pass[0] = 0;
            draw_status(1);
        }
    }
    if (startup) {
        if (!msn.pass[0]) return 0;
    } else if (!get_string(C_EBX, 1, "Password: ", msn.pass)) return 0;
    msg2(C_MNU, "Logging in ...");
    
    while ((r = do_login()) == 1);
    if (r == 0) return 0;
    msn.nfd = -1;
    
    msg(C_ERR, "Failed to connect to server\n");
    return -1;
}

void log_out(int panic)
{
    int retcode = 0;
    struct timespec timeout;
            

    sboard_leave_all(panic);
    if (msn.nfd == -1) return;
    if (panic == 0) {
        timeout = nowplus(5);
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

void show_mbstatus()
{
    LOCK(&msn.lock);
    msg(C_NORMAL, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    UNLOCK(&msn.lock);
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
    Write(msn.nfd, s, strlen(s));
}

void do_ping()
{
    gettimeofday(&tv_ping, NULL);
    msn_png(msn.nfd);
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
        case '\r': sb_blah(); break;
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
        msg(C_DBG, "Loaded %d alias%s\n", c, (c>1)?"es": "");
    } /* file exists */
}

void setup_cfg_dir()
{
    char tmp[SML];
    
    get_home_dir(Config.cfgdir);
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
    int c, paste;
    
    sprintf(copyright_str, "gtmess, version %s (%s), (c) 2002-2004 by George M. Tzoumas", VERSION, VDATE);
    printf("%s\n", copyright_str);
    
    setup_cfg_dir();
    
    msn_ic[0] = msn_ic[1] = (iconv_t) -1;
    
    srand48(time(NULL));

    OConfigTbl = (struct cfg_entry *) Malloc(sizeof(ConfigTbl));
    memcpy(OConfigTbl, ConfigTbl, sizeof(ConfigTbl));
    parse_config(argc, argv);
    msn.nfd = -1;
    config_init();
    
    signal(SIGPIPE, SIG_IGN);
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

    pthread_key_create(&key_sboard, NULL);
    pthread_setspecific(key_sboard, NULL);
    
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
        "(c) 2002-2004 by George M. Tzoumas\n"
        "gtmess is free software, covered by the\nGNU General Public License.\n"
        "There is absolutely no warranty for gtmess.\n\n", VERSION, VDATE);

    load_aliases();

    if (Config.auto_login) log_in(1);
    
    msg2(C_MNU, "Welcome to gtmess");
    
    paste = 0;
    while (1) {
        if (paste == 0) c = getch();
        else paste = 0;
        time(&keyb_time);
        if (!iamback && msn.status == MS_IDL) {
            if (msn.nfd != -1) msn_chg(msn.nfd, nftid(), MS_NLN);
            iamback = 1;
        }
        if (c == KEY_F(10)) break;
        switch (c) {
            case KEY_F(9): {
                char s[SML] = {0};

                msg(C_DBG, "next tid: %u\n", _nftid);
                if (get_string(C_NORMAL, 0, "COMMAND: ", s)) {
                    strcat(s, "\r\n");
                    sendstr(s);
                }
                msg2(C_MNU, copyright_str);
                break;
            }
            case KEY_F(4): { 
                wvis ^= 1; 
                LOCK(&SX);
                draw_sb(SL.head, 1);
                UNLOCK(&SX);
                break; 
            } 
            case KEY_F(5): set_msgskip(-1); break;
            case KEY_F(6): set_msgskip(1); break;
            case KEY_F(7): set_flskip(-1); break;
            case KEY_F(8): set_flskip(1); break;
            case 7: beep(); break; /* ^G */
            case 12: redraw_screen(0); break; /* ^L */
            case KEY_RESIZE: redraw_screen(1); break;
            case 27: /* escape mode */
                msg2(C_MNU, "ESC: (A)dd (C)onn/(D)isconn (S)tat (L)st (N)am (O)pt (M)ail (P)ng (I)nv No(t)e");
                c = tolower(getch());
/*                msg2(C_MNU, copyright_str);*/
/*                usleep(1000);*/
                if (c == KEY_RESIZE) redraw_screen(1);
                else if (c == '0') {
                    c = KEY_F(10);
                    paste = 1;
                } else if (c >= '1' && c <= '9') {
                    int k = c - '0';
                    c = KEY_F(k);
                    paste = 1;
                } else if (c == 'c') {
                    log_in(0);
                    LOCK(&SX);
                    draw_sb(SL.head, 0);
                    UNLOCK(&SX);
                } else if (c == 't') {
                    char note[SML] = {0};
                    if (get_string(C_EBX, 0, "Note:", note) && note[0]) msg(C_NORMAL, "%s\n", note);
                } else if (c == 'o') {
                    menu_msn_options();
                } else if (strchr("sladnmpi/", c) != NULL) {
                    if (c == '/') {
                        char s[SML] = {0};

                        if (get_string(C_EBX, 0, "/", s)) {
                            msg(C_ERR, "Invalid command\n");;
                        }
                        msg2(C_MNU, copyright_str);
                        break;
                    } else if (msn.nfd == -1) msg(C_ERR, "You are not logged in\n");
                    else switch (c) {
                        case 's': menu_change_status(); break;
                        case 'l': menu_lists(); break;
                        case 'a': menu_add(); break;
                        case 'd': log_out(0); break;
                        case 'n': menu_change_name(msn.login, msn.nick); break;
                        case 'm': show_mbstatus(); break;
                        case 'p': do_ping(); break;
                        case 'i': menu_invite(); break;
                    }
                } else if (wvis == 0) sb_keydown(-c); else xf_keydown(-c);
                msg2(C_MNU, copyright_str);
                break;
            default: if (wvis == 0) sb_keydown(c); else xf_keydown(c);
        }
    }
    _gtmess_exit(0);
    return 0;
}
