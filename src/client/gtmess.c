/*
 *    gtmess.c
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

/* TODO
auto filename complete or file selector
what happens if a malicious user puts garbage in clist cache?
show ETA on transfers!
cache notif/passport server 
fix issue with multiple file accepts (after cancel!)
and show both IPs or always remote !
support for unavailable connectivity (act as server for receiving a file)
PNG QNG (keep connection alive!)
MSNP8, MSNP9
fix issue with number of threads spawned, at msnftpd!
filesize limit is 2GB!!!
should we rely on good luck (for lrand48) ?
do not assume always UTF-8 encoding
use iconv() at invitation messages, too!
prove that cleanup handler restores locks :-)
add global ui command line
phone numbers
list menu should support selections (multiple users)
group block/unblock
when no NICK is provided, use the most recently cached one (getnick(dest, login, format))
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
#include<sys/stat.h>
#include<sys/wait.h>

#include<netdb.h>
#include<sys/socket.h>

#include<curses.h>
#include<stdarg.h>
#include<errno.h>
#include<ctype.h>
#include<time.h>

#include<iconv.h>

#include"../inty/inty.h"
#include"md5.h"

#include"screen.h"
#include"editbox.h"
#include"msn.h"
#include"pass.h"
#include"sboard.h"
#include"unotif.h"
#include"xfer.h"
#include"hash_cfg.h"

#include"gtmess.h"

#include"../config.h"

static struct cfg_entry ConfigTbl[] =
  {
    {"log_traffic", "", 0, 1, 0, 1},
    {"colors", "", 0, 1, 0, 2},
    {"sound", "", 1, 1, 0, 2},
    {"popup", "", 1, 1, 0, 1},
    {"time_user_types", "", 5, 1, 1, 60},
/*    {"cvr", "0x0409 win 4.10 i386 MSNMSGR 5.0.0544 MSMSGS null", 0, 0, 0, 0},*/
    {"cvr", "0x0409 linux 2.2.4 i386 GTMESS 0.8.1 MSMSGS null", 0, 0, 0, 0},
    {"cert_prompt", "", 1, 1, 0, 1},
    {"common_name_prompt", "", 1, 1, 0, 1},
    {"console_encoding", "ISO_8859-7", 0, 0, 0, 0},
    {"server", "messenger.hotmail.com", 0, 0, 0, 0},
    {"login", "", 0, 0, 0, 0},
    {"password", "", 0, 0, 0, 0},
    {"initial_status", "", MS_HDN, 1, 0, MS_UNK-1},
    {"online_only", "", 0, 1, 0, 1},
    {"syn_cache", "", 1, 1, 0, 1},
    {"msnftpd", "", 1, 1, 0, 1}
  };

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
#ifdef MSNP8
    int list_count; /* number of LST responses */
#endif
    
    int flskip; /* FL list line skip (for display) */
    pthread_mutex_t lock;

    /* session */
    unsigned int tid;
    int nfd;
    pthread_t thrid;
} msn_t;

typedef struct {
    unsigned int ver; /* list version */
    char blp, gtc;
    int fl, rl, al, bl, gl; /* list length */
} syn_hdr_t;

config_t Config;

syn_hdr_t syn_cache;
FILE *syn_cache_fp = NULL;
char *SP = "";
char MyIP[SML];
char copyright_str[80];
msn_t msn;
pthread_cond_t cond_out = PTHREAD_COND_INITIALIZER;
static struct timeval tv_ping;

FILE *flog;

pthread_mutex_t i_draw_lst_lock;
time_t i_draw_lst_time_start, i_draw_lst_time_end;
int i_draw_lst_handled = 1;
pthread_t ithrid;

void log_out(int panic);
void draw_lst(int r);
void sboard_leave_all(int panic);

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
/*    clear(); refresh();*/
    if (Config.log_traffic) fclose(flog);
    
/*    while (_sboard_kill()); */
    log_out(panic);
    
    pthread_cancel(ithrid);
    pass_done();
    unotif_done();
    if (msn_ic != (iconv_t) -1) iconv_close(msn_ic);
    endwin();
}
#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

RETSIGTYPE gtmess_exit_panic() { 
    _gtmess_exit(1);
    _exit(127);
}


void *interval_daemon(void *dummy)
{
    time_t now;
    while (1) {
        pthread_testcancel();
        sleep(1);
        pthread_testcancel();
        pthread_mutex_lock(&i_draw_lst_lock);
        time(&now);
        if (!i_draw_lst_handled) {
            if (now >= i_draw_lst_time_start) draw_lst(1);
            if (now >= i_draw_lst_time_end) i_draw_lst_handled = 1;
        }
        pthread_mutex_unlock(&i_draw_lst_lock);
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

void draw_status(int r)
{
    scrtxt_t data;
    data.attr = statattrs[msn.status];
    sprintf(data.txt, "%s (%s)", msn.nick, msn_stat_name[msn.status]);
    scr_event(SCR_TOPLINE, &data, sizeof(data), r);
}

void draw_lst(int r)
{
    scrlst_t data;
    
    msn_clist_cpy(&data.p, &msn.FL);
    msn_glist_cpy(&data.g, &msn.GL);
    data.skip = msn.flskip;
    scr_event(SCR_LST, &data, sizeof(data), 0);
    scr_event(SCR_RLST, NULL, 0, r);
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

void draw_all()
{
    scr_event(SCR_RMSG, &w_msg, 0, 0);
    draw_status(0);
    draw_lst(0);
    draw_sb(SL.head, 0);
    scr_event(SCR_DRAWREST, NULL, 0, 1);
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

int read_syn_cache_hdr()
{
    char fn[SML];
    
    sprintf(fn, "%s/%s/syn", Config.cfgdir, msn.login);
    if ((syn_cache_fp = fopen(fn, "r")) != NULL &&
        fread(&syn_cache, sizeof(syn_hdr_t), 1, syn_cache_fp) == 1) {
            return 0;
    }
    syn_cache.ver = 0;
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
                    msg(C_MSG, "%s <%s> has added you to his/her contact list\n", q->nick, q->login);
        }
    msg(C_MSG, "FL/RL contacts: %d/%d\n", msn.FL.count, msn.RL.count);
    for (p = msn.FL.head; p != NULL; p = p->next)
        if (msn_clist_find(&msn.RL, p->login) == NULL)
            /* bug: multiple same warnings, when user on multiple groups! */
            msg(C_MSG, "%s <%s> has removed you from his/her contact list\n",
                p->nick, p->login);
    /* update FL with IL */
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
}

void msn_init(msn_t *msn)
{
    strcpy(msn->nick, msn->login);
    msn->status = MS_FLN;
    msn->inbox = 0;
    msn->folders = 0;

    msn->BLP = msn->GTC = 0;

    msn->GL.head = NULL; msn->GL.count = 0;
    msn->FL.head = NULL; msn->FL.count = 0;
    msn->RL.head = NULL; msn->RL.count = 0;
    msn->AL.head = NULL; msn->AL.count = 0;
    msn->BL.head = NULL; msn->BL.count = 0;

    msn->IL.head = NULL; msn->IL.count = 0;

#ifdef MSNP8    
    msn->list_count = -1;
#endif
    msn->flskip = 0;
    msn->nfd = -1;
    msn->thrid = -1;
}

void msn_handle_msgbody(char *msgdata, int len)
{
    char tmp[SML];
    char *s;

    tmp[0] = 0;
    s = strafter(msgdata, "Content-Type: ");
    sscanf(s, "%s", tmp);

    if (strstr(tmp, "text/x-msmsgsinitialemailnotification") != NULL) {
    /* initial email notification */
        s = strafter(msgdata, "\nInbox-Unread: ");
        sscanf(s, "%d", &msn.inbox);
        s = strafter(msgdata, "\nFolders-Unread: ");
        sscanf(s, "%d", &msn.folders);

        msg(C_MSG, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    } else if (strstr(tmp, "text/x-msmsgsemailnotification") != NULL) {
    /* new email */
        char from[SML], subject[SML], folder[SML];

        from[0] = subject[0] = folder[0] = 0;
        s = strafter(msgdata, "\nFrom: ");
        sscanf(s, "%[^\r]", from);
        s = strafter(msgdata, "\nSubject: ");
        sscanf(s, "%[^\r]", subject);
        s = strafter(msgdata, "\nDest-Folder: ");
        sscanf(s, "%[^\r]", folder);

        if (strcmp(folder, "ACTIVE") == 0) msn.inbox++;
        else if (strstr(folder, "trAsH") == NULL) msn.folders++;
        msg(C_MSG, "NEW MAIL: From: %s, Subject: %s\n", from, subject);
        msg(C_MSG, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
        unotify("New mail\n", SND_NEWMAIL);
    } else if (strstr(tmp, "text/x-msmsgsprofile") != NULL) {
    /* user profile */
    } else if (strstr(tmp, "text/x-msmsgsactivemailnotification") != NULL) {
    /* mailbox traffic */
        char src[SML], dest[SML];
        int idelta, fdelta, d;

        src[0] = dest[0] = 0;
        s = strafter(msgdata, "\nSrc-Folder: ");
        sscanf(s, "%[^\r]", src);
        s = strafter(msgdata, "\nDest-Folder: ");
        sscanf(s, "%[^\r]", dest);
        s = strafter(msgdata, "-Delta:");
        sscanf(s, "%d", &d);

        if (strcmp(src, "ACTIVE") == 0) idelta = -d;
        else if (strcmp(dest, "ACTIVE") == 0) idelta = d;
        else idelta = 0;

        if (strcmp(src, "ACTIVE") != 0 && strstr(src, "trAsH") == NULL) fdelta = -d;
        else if (strcmp(dest, "ACTIVE") != 0 && strstr(dest, "trAsH") == NULL) fdelta = d;
        else fdelta = 0;

        msn.inbox += idelta;
        msn.folders += fdelta;
        if (msn.inbox < 0) msn.inbox = 0;
        if (msn.folders < 0) msn.folders = 0;
        msg(C_MSG, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    } else if (strstr(tmp, "application/x-msmsgssystemmessage") != NULL) {
        
    /* system message */    
        
        int type = -1, arg1 = -1;
        s = strafter(msgdata, "\nType: "); sscanf(s, "%d", &type);
        s = strafter(msgdata, "\nArg1: "); sscanf(s, "%d", &arg1);
        if (type == 1) msg(C_MSG, "The server is going down for maintenance in %d minutes", arg1);
        else msg(C_DBG, msgdata);
    } else msg(C_DBG, msgdata);
}

void msn_sb_handle_msgbody(char *arg1, char *arg2, char *msgdata, int len)
{
    char ctype[SML], nick[SML];
    char *s;
    msn_sboard_t *sb = pthread_getspecific(key_sboard);

    if (strchr(arg1, '@') != NULL) url2str(arg2, nick);
    else sprintf(nick, "<%s>", arg1);
    ctype[0] = 0;
    s = strafter(msgdata, "Content-Type: ");
    if (s == NULL) {
        dlg(C_DBG, msgdata);
        return;
    }
    sscanf(s, "%s", ctype);

    if (strstr(ctype, "text/plain") != NULL) {
        
    /* user chats */
        char *text;
        
        s = strafter(msgdata, "\r\n\r\n");
        text = (char *) Malloc(len - (s - msgdata) + 1);
        sb->pending = 1;
        
        utf8decode(sb->ic, s, text);
        dlg(C_DBG, "%s says:\n", nick);
        dlg(C_MSG, "%s\n", text);
        free(text);
        return;
    }
    
    if (strstr(ctype, "text/x-msmsgscontrol") != NULL) {
        
    /* user types */
        time_t now;
        
        time(&now);
        msn_clist_update(&msn.FL, arg1, NULL, -1, -1, now);
        draw_lst(1);
        /* rather tricky! (in order to be drawn *after* time_user_types sec) */
        sched_draw_lst(Config.time_user_types + 1); 
        return;
    }
    
    if (strstr(ctype, "text/x-msmsgsinvite") != NULL) {
        
    /* invitation */
        xstat_t xs;
        unsigned int size;
        
        if ((s = strafter(msgdata, "\nInvitation-Command: ")) != NULL) {
            char ic[SML];
            unsigned int cookie;
            xfer_t *x;
            
            sscanf(s, "%s", ic);
            if (strcmp(ic, "INVITE") == 0) xs = XS_INVITE;
            else if (strcmp(ic, "ACCEPT") == 0) xs = XS_ACCEPT;
            else if (strcmp(ic, "CANCEL") == 0) xs = XS_CANCEL;
            else xs = XS_UNKNOWN;
            
            s = strafter(msgdata, "\nInvitation-Cookie: ");
            if (sscanf(s, "%u", &cookie) == 0) return;
            
            if (xs == XS_INVITE) {
                if (strstr(msgdata, "\nApplication-GUID: {5D3E02AB-6190-11d3-BBBB-00C04F795683}") != NULL) {
                    
                /* file transfer */
                    char fname[SML], *bfname;
                    
                    s = strafter(msgdata, "\nApplication-File: ");
                    sscanf(s, "%s", fname);
                    if ((bfname = strrchr(fname, '/')) != NULL) bfname++;
                    else bfname = fname;
                    s = strafter(msgdata, "\nApplication-FileSize: ");
                    sscanf(s, "%u", &size);
                    LOCK(&XX);
                    xfl_add_file(&XL, XS_INVITE, msn.login, arg1, 1, cookie, sb, bfname, size);
                    UNLOCK(&XX);
                    sb->pending = 1;
                    dlg(C_DBG, "%s wants to send you the file '%s', %u bytes long\n", nick, fname, size);
                } else {
                /* reject all other types of invitations */
                    dlg(C_DBG, msgdata);
                    msn_msg_cancel(sb->sfd, sb->tid++, cookie, "REJECT_NOT_INSTALLED");
                }
                draw_sb(sb, 1);
                return;
            }
            
            LOCK(&XX);
            x = xfl_find(&XL, cookie);
            if (x == NULL || x->alive) {
                UNLOCK(&XX);
                return;
            }
            
            if (xs == XS_ACCEPT && x->xclass == XF_FILE) {
                if (x->incoming) {
                    int port;
                    unsigned int auth;
                    char ip[SML];
                    pthread_t thr;

                    s = strafter(msgdata, "\nIP-Address: ");
                    ip[0] = 0;
                    sscanf(s, "%s", ip);
                    s = strafter(msgdata, "\nPort: ");
                    sscanf(s, "%d", &port);
                    s = strafter(msgdata, "\nAuthCookie: ");
                    sscanf(s, "%u", &auth);

                    x->data.file.port = port;
                    x->data.file.auth_cookie = auth;
                    strcpy(x->data.file.ipaddr, ip);
                    x->status = xs;
                    pthread_create(&thr, NULL, msnftp_client, (void *) x);
                    pthread_detach(thr);
                } else {
                    x->status = xs;
                    strcpy(x->data.file.ipaddr, MyIP);
                    strcpy(x->remote, arg1);
                    x->data.file.port = MSNFTP_PORT;
                    x->data.file.auth_cookie = lrand48();
                    msn_msg_accept2(sb->sfd, sb->tid++, x->inv_cookie, x->data.file.ipaddr, 
                                    x->data.file.port, x->data.file.auth_cookie);

                }
            } else if (xs == XS_CANCEL) {
                char cc[SML];

                s = strafter(msgdata, "\nCancel-Code: ");
                sscanf(s, "%s", cc);
                if (strcmp(cc, "REJECT") == 0) xs = XS_REJECT;
                else if (strcmp(cc, "TIMEOUT") == 0) xs = XS_TIMEOUT;
                else if (strcmp(cc, "REJECT_NOT_INSTALLED") == 0) xs = XS_REJECT_NOT_INSTALLED;
                else if (strcmp(cc, "FTTIMEOUT") == 0) xs = XS_FTTIMEOUT;
                else if (strcmp(cc, "FAIL") == 0) xs = XS_FAIL;
                else if (strcmp(cc, "OUTBANDCANCEL") == 0) xs = XS_OUTBANDCANCEL;

                if (x->xclass == XF_FILE) {
                    if (x->incoming)
                        dlg(C_DBG, "%s doesn't want to send you '%s' any more\n", nick, x->data.file.fname);
                    else
                        dlg(C_DBG, "%s doesn't want to receive '%s'\n", nick, x->data.file.fname);
                }
                x->status = xs;
            } else {
                x->status = xs;
            }
            UNLOCK(&XX);
        } else {
            dlg(C_DBG, msgdata);
        }
        draw_sb(sb, 1);
        return;
    }
    
    dlg(C_DBG, msgdata);
    sb->pending = 1;
}

int scan_error(char *s)
{
    int err;

    if (sscanf(s, "%d", &err) == 1) {
        msg(C_ERR, "SERVER ERROR %d: %s\n", err, msn_error_str(err));
        return err;
    }
    return 0;
}

int scan_error_sb(char *s)
{
    int err;

    if (sscanf(s, "%d", &err) == 1) {
        dlg(C_ERR, "SERVER ERROR %d: %s\n", err, msn_error_str(err));
        return err;
    }
    return 0;
}

void logout_cleanup(void *r)
{
    LOCK(&msn.lock);
    
    close(msn.nfd);
    msn.nfd = -1;
    msn_glist_free(&msn.GL);
    msn_clist_free(&msn.FL);
    msn_clist_free(&msn.AL);
    msn_clist_free(&msn.BL);
    msn_clist_free(&msn.RL);
    msn_clist_free(&msn.IL);
    msn_init(&msn);
    
    LOCK(&SX);
    draw_status(0);
    draw_lst(1);
/*    draw_all();*/
    UNLOCK(&SX);
    pthread_cond_signal(&cond_out);
    UNLOCK(&msn.lock);
    
    if (((int) r) == 0 || ((int) r) == -2) msg(C_ERR, "msn_ndaemon(): disconnected from server\n");
    unotify("You have been signed out\n", SND_LOGOUT);
/*    msg(C_DBG, "msn_ndaemon(): flushed %d bytes\n", buf.len);*/
}

void sb_cleanup(void *arg)
{
    msn_sboard_t *sb = (msn_sboard_t *) arg;

    close(sb->sfd);
    sb->sfd = -1;
    msn_clist_free(&sb->PL);
    sb->thrid = -1;
    sb->tid = 0;
}

int do_connect(char *server)
{
    int sfd;

    if ((sfd = ConnectToServer(server, 1863)) < 0)
        msg(C_ERR, "%s\n", strerror(errno));
    else msg(C_MSG, "Connected\n");
    return sfd;
}

int do_connect_sb(char *server)
{
    int sfd;

    if ((sfd = ConnectToServer(server, 1863)) < 0)
        dlg(C_ERR, "%s\n", strerror(errno));
    else dlg(C_DBG, "Connected\n");
    return sfd;
}

void *msn_sbdaemon(void *arg)
{
    msn_sboard_t *sb = (msn_sboard_t *) arg;
    buffer_t buf;
    char s[SXL];
    char com[SML], arg1[SML], arg2[SML], nick[SML];
/*    msn_contact_t *p;*/
    int msglen;
    char *msgdata;
    int r;

    pthread_setspecific(key_sboard, arg);
    LOCK(&SX);
    if (sb->called) dlg(C_DBG, "Invitation from <%s>\n", sb->master);
    dlg(C_NORMAL, "Connecting to switchboard %s ...\n", sb->sbaddr);
    sb->sfd = do_connect_sb(sb->sbaddr);
    UNLOCK(&SX);
    if (sb->sfd < 0) {
        sb->thrid = -1;
        return NULL;
    }
    if (sb->called) {
        LOCK(&msn.lock);
        sprintf(s, "ANS %u %s %s %s\r\n", sb->tid++, msn.login, sb->hash, sb->sessid);
        UNLOCK(&msn.lock);
        Write(sb->sfd, s, strlen(s));
    } else {
        LOCK(&msn.lock);
        sprintf(s, "USR %u %s %s\r\n", sb->tid++, msn.login, sb->hash);
        UNLOCK(&msn.lock);
        Write(sb->sfd, s, strlen(s));
    }

    bfAssign(&buf, sb->sfd);
    pthread_cleanup_push(sb_cleanup, arg);
    while (1) {
        r = bfParseLine(&buf, s, SXL);
        LOCK(&SX);
        if (r == -2) dlg(C_ERR, "bfParseLine(): buffer overrun\n");
        else if (r < 0) dlg(C_ERR, "bfParseLine(): %s\n", strerror(errno));
        UNLOCK(&SX);
        if (r <= 0) break;

        com[0] = 0; arg1[0] = 0; arg2[0] = 0;
        msglen = 0; msgdata = NULL;

        sscanf(s, "%s", com);
        if (is3(com, "MSG")) {

        /* message */

            if (sscanf(s + 4, "%s %s %d", arg1, arg2, &msglen) == 3) {
                msgdata = (char *) Malloc(msglen+1);
                r = bfFlushN(&buf, msgdata, msglen);
                bfReadX(&buf, msgdata + r, msglen - r);
                msgdata[msglen] = 0;
                LOCK(&msn.lock);
                LOCK(&SX);
                msn_sb_handle_msgbody(arg1, arg2, msgdata, msglen);
                UNLOCK(&SX);
                UNLOCK(&msn.lock);
                free(msgdata);
            } else {
                LOCK(&SX);
                dlg(C_ERR, "msn_sbdaemon(): could not parse message\n");
                UNLOCK(&SX);
            }
        } else if (is3(com, "JOI")) {

        /* user joins conversation */

            sscanf(s + 4, "%s %s", arg1, arg2);
            url2str(arg2, nick);
            LOCK(&SX);
            msn_clist_add(&sb->PL, -1, arg1, nick);
            if (sb->PL.count > 1) sb->multi = 1;
            if (sb->PL.count >= 0) sb->cantype = 1;
            if (sb->PL.count == 1) {
                strcpy(sb->master, arg1);
                strcpy(sb->mnick, nick);
            }
            draw_plst(sb, 0);
            dlg(C_DBG, "%s <%s> has joined the conversation\n", nick, arg1);
            UNLOCK(&SX);
        } else if (is3(com, "IRO")) {

        /* participant list */

            int index;

            sscanf(s + 4, "%*u %d %*d %s %s", &index, arg1, arg2);
            url2str(arg2, nick);
            LOCK(&SX);
            msn_clist_add(&sb->PL, -1, arg1, nick);
            draw_plst(sb, 0);
            if (sb->PL.count > 1) sb->multi = 1;
            dlg(C_DBG, "%d. %s <%s>\n", index, nick, arg1);
            UNLOCK(&SX);
        } else if (is3(com, "USR")) {
            LOCK(&SX);
            sb->cantype = 1;
            dlg(C_DBG, "Ready to invite other users\n");
            UNLOCK(&SX);
        } else if (is3(com, "ANS")) {

        /* end of initial negotiation */

            LOCK(&SX);
            sb->cantype = 1;
            dlg(C_DBG, "total participants: %d\n", sb->PL.count);
            UNLOCK(&SX);
        } else if (is3(com, "CAL")) {

        /* somebody gets called */

            sscanf(s + 4, "%*u %*s %s", arg1);
            LOCK(&SX);
            strcpy(sb->sessid, arg1);
            dlg(C_DBG, "Calling other party ...\n");
            UNLOCK(&SX);
        } else if (is3(com, "BYE")) {

        /* user leaves chat */

            int leave;
/*            int is_offline;
            msn_contact_t *p;*/

            sscanf(s + 4, "%s", arg1);
            LOCK(&msn.lock);
            LOCK(&SX);
/*            sb->pending = 1;*/
            msn_clist_rem(&sb->PL, arg1, -1);
/*            p = msn_clist_find(&msn.FL, arg1);
            is_offline = p != NULL && p->status == MS_FLN;
            leave = sb->PL.count == 0 && (sb->multi || is_offline);*/
            leave = sb->PL.count == 0 && sb->multi;
            draw_plst(sb, 0);
            dlg(C_ERR, "<%s> has left the conversation\n", arg1);
            UNLOCK(&SX);
            UNLOCK(&msn.lock);
            if (leave) break;
        } else if (is3(com, "NAK")) {

        /* negative acknowlegement */

            unsigned int id;

            sscanf(s + 4, "%u", &id);
            LOCK(&SX);
            dlg(C_ERR, "A previous message could not be delivered\n"
                       "Message-id: %u, Current-id: %u\n", id, sb->tid);
            UNLOCK(&SX);
        } else {

        /* error code */

            LOCK(&SX);
            sb->pending = 1;
            if (!scan_error_sb(com))
                dlg(C_DBG, "<<< %s", s);
            UNLOCK(&SX);
        }
    }
    pthread_cleanup_pop(0);
    LOCK(&SX);
    sb_cleanup(arg);
/*    sb->pending = 1;*/
    sb->cantype = 0;
/*    snd_play(SND_PENDING);*/
    draw_plst(sb, 0);
    dlg(C_ERR, "Disconnected from switchboard\n");
    if (SL.out_count > 0) SL.out_count--;
    if (SL.out_count == 0) pthread_cond_signal(&SL.out_cond);
    UNLOCK(&SX);
    pthread_detach(pthread_self());
    return NULL;
}

void *msn_ndaemon(void *dummy)
{
    buffer_t buf;
    char s[SXL];
    char com[SML], arg1[SML], arg2[SML], nick[SML];
    msn_contact_t *p, *q;
    int msglen;
    char *msgdata;
    int r;

    bfAssign(&buf, msn.nfd);
    pthread_cleanup_push(logout_cleanup, (void *) 0);
    while (1) {
        r = bfParseLine(&buf, s, SXL);
        if (r == -2) msg(C_ERR, "bfParseLine(): buffer overrun\n");
        else if (r < 0) msg(C_ERR, "bfParseLine(): %s\n", strerror(errno));
/*        pthread_testcancel();*/
/*        if (r >= 0) msg("bfParseLine(): parsed %d bytes\n", r);*/
        if (Config.log_traffic && r > 0) {
            struct tm t;
            time_t t1;
            time(&t1);
            localtime_r(&t1, &t);
            fprintf(flog, "%d-%02d-%02d %02d:%02d:%02d< %s", 1900+t.tm_year, 1+t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, s);
        }
        if (r <= 0) break;

        com[0] = 0; arg1[0] = 0; arg2[0] = 0;
        msglen = 0; msgdata = NULL;

/*        msg(C_DBG, "<<< %s", s);*/

        sscanf(s, "%s", com);
        if (is3(com, "MSG")) {

        /* message */

            if (sscanf(s + 4, "%s %s %d", arg1, arg2, &msglen) == 3) {
                msgdata = (char *) Malloc(msglen+1);
                r = bfFlushN(&buf, msgdata, msglen);
                bfReadX(&buf, msgdata + r, msglen - r);
                msgdata[msglen] = 0;
                msn_handle_msgbody(msgdata, msglen);
                free(msgdata);
            } else msg(C_ERR, "msn_ndaemon(): could not parse message\n");
        } else if (is3(com, "CHL")) {

        /* challenge */

            sscanf(s + 4, "%*s %s", arg2);
            msn_qry(msn.nfd, nftid(), arg2);
        } else if (is3(com, "CHG")) {

        /* status change */

            sscanf(s + 4, "%*s %s", arg2);
            LOCK(&msn.lock);
            msn.status = msn_stat_id(arg2);
            draw_status(0);
            UNLOCK(&msn.lock);
            msg(C_MSG, "Your status has changed to %s\n", msn_stat_name[msn.status]);
        } else if (is3(com, "GTC")) {

        /* reverse list prompting */

#ifdef MSNP8
            sscanf(s + 4, "%s", arg2);
#else
            sscanf(s + 4, "%*s %*s %s", arg2);
#endif
            LOCK(&msn.lock);
            msn.GTC = arg2[0];
            UNLOCK(&msn.lock);
            msg(C_MSG, "Prompt when other users add you: %s\n", arg2[0] == 'N'? "NEVER": "ALWAYS");
        } else if (is3(com, "BLP")) {

        /* privacy mode */

#ifdef MSNP8
            sscanf(s + 4, "%s", arg2);
#else
            sscanf(s + 4, "%*s %*s %s", arg2);
#endif
            LOCK(&msn.lock);
            msn.BLP = arg2[0];
            UNLOCK(&msn.lock);
            msg(C_MSG, "All other users: %s\n", arg2[0] == 'B'? "BLOCKED": "ALLOWED");
        } else if (is3(com, "REG")) {

        /* group rename */

            int gid;
            msn_group_t *pg;

            sscanf(s + 4, "%*s %*s %d %s", &gid, arg2);
            LOCK(&msn.lock);
            url2str(arg2, nick);
            if ((pg = msn_glist_find(&msn.GL, gid)) != NULL) {
                strcpy(pg->name, nick);
                draw_lst(1);
                msg(C_MSG, "Group %d renamed to %s\n", gid, nick);
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "ADG")) {

        /* group add */

            int gid;

            sscanf(s + 4, "%*s %*s %s %d", arg2, &gid);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            msn_glist_add(&msn.GL, gid, nick);
            draw_lst(1);
            msg(C_MSG, "Added group %s with id %d\n", nick, gid);
            UNLOCK(&msn.lock);
        } else if (is3(com, "RMG")) {

        /* group remove*/

            int gid, nr;

            sscanf(s + 4, "%*s %*s %d", &gid);
            LOCK(&msn.lock);
            msn_glist_rem(&msn.GL, gid);
            nr = msn_clist_rem_grp(&msn.FL, &msn.RL, gid);
            draw_lst(1);
            msg(C_MSG, "Group [%d] removed with %d contacts\n", gid, nr);
            UNLOCK(&msn.lock);
        } else if (is3(com, "LSG")) {

        /* group list */

            int gid;

#ifdef MSNP8
            sscanf(s + 4, "%d %s", &gid, arg2);
#else
            sscanf(s + 4, "%*s %*s %*s %*s %d %s", &gid, arg2);
#endif
            url2str(arg2, nick);
            LOCK(&msn.lock);
            msn_glist_add(&msn.GL, gid, nick);
            UNLOCK(&msn.lock);
        } else if (is3(com, "ILN")) {

        /* initial status of FL entries */

            char stat[SML];

            LOCK(&msn.lock);
            sscanf(s + 4, "%*u %s %s %s", stat, arg1, arg2);
            url2str(arg2, nick);
            if (msn_clist_update(&msn.FL, arg1, nick, msn_stat_id(stat), -1, -1) > 0) {
                draw_lst(1);
            } else {
                p = msn_clist_add(&msn.IL, -1, arg1, nick);
                p->status = msn_stat_id(stat);
            }
            UNLOCK(&msn.lock);
            msg(C_MSG, "%s <%s> is %s\n", nick, arg1, msn_stat_name[msn_stat_id(stat)]);
        } else if (is3(com, "NLN")) {

        /* contact changes to online status */

            char stat[SML], nf[SML];
            msn_stat_t old = MS_UNK;

            LOCK(&msn.lock);
            sscanf(s + 4, "%s %s %s", stat, arg1, arg2);
            url2str(arg2, nick);
            if ((p = msn_clist_find(&msn.FL, arg1)) != NULL) old = p->status;
            if (old < MS_NLN) {
                sprintf(nf, "%s is %s\n", nick, msn_stat_name[msn_stat_id(stat)]);
                unotify(nf, SND_ONLINE);
            }
            if (msn_clist_update(&msn.FL, arg1, nick, msn_stat_id(stat), -1, -1) > 0) {
                draw_lst(1);
                msg(C_MSG, "%s <%s> is %s\n", nick, arg1, msn_stat_name[msn_stat_id(stat)]);
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "FLN")) {

        /* contact appears offline */
            char nf[SML];

            LOCK(&msn.lock);
            sscanf(s + 4, "%s", arg1);
            p = msn_clist_find(&msn.FL, arg1);
            if (p != NULL && p->status == MS_FLN)
                msg(C_ERR, "%s <%s> possibly appears offline\n", p->nick, arg1);
            else if (msn_clist_update(&msn.FL, arg1, NULL, MS_FLN, -1, -1) > 0) {
                draw_lst(1);
                msg(C_MSG, "%s <%s> is %s\n", p->nick, arg1, msn_stat_name[MS_FLN]);
                sprintf(nf, "%s is Offline\n", p->nick);
                unotify(nf, SND_OFFLINE);
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "REM")) {

        /* list remove */

            char lst[SML];
            int grp = -1;

            lst[0] = 0;
            sscanf(s + 4, "%*u %s %*d %s %d", lst, arg1, &grp);
            switch (lst[0]) {
                case 'F':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.FL, arg1, grp);
                    if ((p = msn_clist_find(&msn.RL, arg1)) != NULL)
                        p->gid--;
                    draw_lst(1);
                    msg(C_MSG, "<%s> has been removed from group %s\n",
                            arg1, msn_glist_findn(&msn.GL, grp));
                    UNLOCK(&msn.lock);
                    break;
                case 'A':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.AL, arg1, -1);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "<%s> has been removed from your allow list\n", arg1);
                    break;
                case 'B':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.BL, arg1, -1);
                    if (msn_clist_update(&msn.FL, arg1, NULL, -1, 0, -1) > 0)
                        draw_lst(1);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "<%s> has been unblocked\n", arg1);
                    break;
                case 'R':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.RL, arg1, -1);
                    UNLOCK(&msn.lock);
                    break;
                    msg(C_MSG, "<%s> has removed you from his/her contact list\n", arg1);
            }
        } else if (is3(com, "ADD")) {

        /* list add */

            int gid;
            char lst[SML];
            msn_contact_t *q;

            lst[0] = 0;
            sscanf(s + 4, "%*u %s %*d %s %s %d", lst, arg1, arg2, &gid);
            url2str(arg2, nick);
            switch (lst[0]) {
                case 'F':
                    LOCK(&msn.lock);
                    p = msn_clist_find(&msn.FL, arg1);
                    q = msn_clist_add(&msn.FL, gid, arg1, nick);
                    if (p != NULL) {
                        q->status = p->status;
                        strcpy(q->nick, p->nick);
                    }
                    if ((p = msn_clist_find(&msn.RL, arg1)) != NULL)
                        p->gid++;
                    if (msn_clist_find(&msn.BL, arg1) != NULL)
                        q->blocked = 1;
                    draw_lst(1);
                    msg(C_MSG, "%s <%s> has been added to group %s\n",
                            nick, arg1, msn_glist_findn(&msn.GL, gid));
                    UNLOCK(&msn.lock);
                    break;
                case 'A':
                    LOCK(&msn.lock);
                    msn_clist_add(&msn.AL, -1, arg1, nick);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been added to your allow list\n", nick, arg1);
                    break;
                case 'B':
                    LOCK(&msn.lock);
                    msn_clist_add(&msn.BL, -1, arg1, nick);
                    if (msn_clist_update(&msn.FL, arg1, NULL, -1, 1, -1) > 0)
                        draw_lst(1);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been blocked\n", nick, arg1);
                    break;
                case 'R':
                    LOCK(&msn.lock);
                    q = msn_clist_add(&msn.RL, 0, arg1, nick);
                    for (p = msn.FL.head; p != NULL; p = p->next)
                        if (strcmp(arg1, p->login) == 0) q->gid++;
                    if (msn.GTC == 'A') 
                        msg(C_MSG, "%s <%s> has added you to his/her contact list\n", nick, arg1);
                    UNLOCK(&msn.lock);
                    break;
            }
        } else if (is3(com, "LST")) {

        /* contact lists */

#ifdef MSNP8
            int lst, gid;
            char gids[SML], *tmp;
            
            sscanf(s + 4, "%s %s %d %s", arg1, arg2, &lst, gids);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            if (lst & 1) {
                /* FL */
                tmp = gids;
                while (1) {
                    if (sscanf(tmp, "%d", &gid) == 1)
                        msn_clist_add(&msn.FL, gid, arg1, nick);
                    else break;
                    tmp = strchr(tmp, ',');
                    if (tmp == NULL) break; else tmp++;
                }
            }
            if (lst & 2) {
                /* AL */
                msn_clist_add(&msn.AL, -1, arg1, nick);
            }
            if (lst & 4) {
                /* BL */
                msn_clist_add(&msn.BL, -1, arg1, nick);
                if ((p = msn_clist_find(&msn.FL, arg1)) != NULL)
                    p->blocked = 1;
            }
            if (lst & 8) {
                /* RL */
                q = msn_clist_add(&msn.RL, 0, arg1, nick);
                if ((p = msn_clist_find(&msn.FL, arg1)) == NULL) {
                    if (msn.GTC == 'A' && msn_clist_find(&msn.AL, arg1) == NULL &&
                        msn_clist_find(&msn.BL, arg1) == NULL)
                            msg(C_MSG, "%s <%s> has added you to his/her contact list\n",
                                nick, arg1);
                } else for (p = msn.FL.head; p != NULL; p = p->next)
                    if (strcmp(arg1, p->login) == 0) q->gid++;
            }
            if (--msn.list_count == 0) {
            /* end of sync -- last LST rmember */
                msg(C_MSG, "FL/RL contacts: %d/%d\n", msn.FL.count, msn.RL.count);
                for (p = msn.FL.head; p != NULL; p = p->next)
                    if (msn_clist_find(&msn.RL, p->login) == NULL)
                        /* bug: multiple same warnings, when user on multiple groups! */
                        msg(C_MSG, "%s <%s> has removed you from his/her contact list\n",
                            p->nick, p->login);

                /* update FL with IL */
                for (p = msn.IL.head; p != NULL; p = p->next)
                    msn_clist_update(&msn.FL, p->login, p->nick, p->status, -1, -1);
                msn_clist_free(&msn.IL);

                draw_lst(1);
                write_syn_cache();
            }
            UNLOCK(&msn.lock);
#else
            int c, gid;
            char lst[SML], gids[SML], *tmp;

            lst[0] = 0;
            sscanf(s + 4, "%*u %s %*d %*d %d %s %s %s", lst, &c, arg1, arg2, gids);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            switch (lst[0]) {
                case 'F':
                    if (c == 0) break;
                    tmp = gids;
                    while (1) {
                        if (sscanf(tmp, "%d", &gid) == 1)
                            msn_clist_add(&msn.FL, gid, arg1, nick);
                        else break;
                        tmp = strchr(tmp, ',');
                        if (tmp == NULL) break; else tmp++;
                    }
                    break;
                case 'A':
                    if (c == 0) break;
                    msn_clist_add(&msn.AL, -1, arg1, nick);
                    break;
                case 'B':
                    if (c == 0) break;
                    msn_clist_add(&msn.BL, -1, arg1, nick);
                    if ((p = msn_clist_find(&msn.FL, arg1)) != NULL)
                        p->blocked = 1;
                    break;
                case 'R':
                    if (c == 0) {
                        draw_lst(1);
                        break;
                    }
                    q = msn_clist_add(&msn.RL, 0, arg1, nick);
                    if ((p = msn_clist_find(&msn.FL, arg1)) == NULL) {
                        if (msn.GTC == 'A' && msn_clist_find(&msn.AL, arg1) == NULL &&
                            msn_clist_find(&msn.BL, arg1) == NULL)
                                msg(C_MSG, "%s <%s> has added you to his/her contact list\n",
                                    nick, arg1);
                    } else for (p = msn.FL.head; p != NULL; p = p->next)
                        if (strcmp(arg1, p->login) == 0) q->gid++;
                    if (msn.RL.count == c) {
                        msg(C_MSG, "FL/RL contacts: %d/%d\n", msn.FL.count, msn.RL.count);
                    /* end of sync -- last RL member */
                        for (p = msn.FL.head; p != NULL; p = p->next)
                            if (msn_clist_find(&msn.RL, p->login) == NULL)
                                /* bug: multiple same warnings, when user on multiple groups! */
                                msg(C_MSG, "%s <%s> has removed you from his/her contact list\n",
                                    p->nick, p->login);

                        /* update FL with IL */
                        for (p = msn.IL.head; p != NULL; p = p->next)
                            msn_clist_update(&msn.FL, p->login, p->nick, p->status, -1, -1);
                        msn_clist_free(&msn.IL);

                        draw_lst(1);
                        write_syn_cache();
                    }
                    break;
            }
            UNLOCK(&msn.lock);
#endif
        } else if (is3(com, "SYN")) {

        /* sync begin */
            sscanf(s + 4, "%*u %u %d", &msn.SYN, &msn.list_count);
            if (msn.SYN == syn_cache.ver) {
                LOCK(&msn.lock);
                read_syn_cache_data();
                msg(C_MSG, "Contacts loaded\n");
                draw_lst(1);
                UNLOCK(&msn.lock);
            } else {
                if (syn_cache_fp != NULL) fclose(syn_cache_fp);
                msg(C_MSG, "Retrieving contacts ...\n");
            }
            
        } else if (is3(com, "PRP") /* personal phone numbers (ignore) */
                    || is3(com, "BPR") /* user phone numbers */
                    || is3(com, "QRY")) { /* challenge accepted */
            
            /* ignore */
            
        } else if (is3(com, "QNG")) {
            
        /* pong */
            
            struct timeval tv;
            double sec;
            int isec;
            
            gettimeofday(&tv, NULL);
            isec = tv.tv_sec - tv_ping.tv_sec;
            sec = ((double) isec + ((double) (tv.tv_usec - tv_ping.tv_usec)) / 1000000);
            msg(C_MSG, "RTT = %g sec\n", sec);
        } else if (is3(com, "REA")) {

        /* screen name change */

            sscanf(s + 4, "%*u %*d %s %s", arg1, arg2);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            if (strcmp(arg1, msn.login) == 0) {
                strcpy(msn.nick, nick);
                draw_status(1);
            } else if (msn_clist_update(&msn.FL, arg1, nick, -1, -1, -1) > 0)
                draw_lst(1);
            UNLOCK(&msn.lock);
        } else if (is3(com, "XFR")) {

        /* referral */
            char addr[SML];

            addr[0] = 0;
            sscanf(s + 4, "%*u %s %s %*s %s", arg1, addr, arg2);
            if (arg1[0] == 'N') {
                msg(C_ERR, "SERVER: You must connect connect to a new notification server\n"
                           "NEW ADDRESS: %s\n", addr);
                LOCK(&msn.lock);
                strcpy(msn.notaddr, addr);
                UNLOCK(&msn.lock);
                break;
            } else if (arg1[0] == 'S') {
                msn_sboard_t *sb;

                LOCK(&SX);
                sb = msn_sblist_add(&SL, addr, arg2, SP, SP, 0, SP);
                SL.head = sb;
                draw_sb(sb, 1);
                UNLOCK(&SX);
                pthread_create(&sb->thrid, NULL, msn_sbdaemon, (void *) sb);
/*                pthread_detach(sb->thrid);*/
            } else msg(C_DBG, "<<< %s", s);
        } else if (is3(com, "RNG")) {

        /* invitation to switchboard */
            char sbaddr[SML], hash[SML], sessid[SML], nf[SML];
            msn_sboard_t *sb;
            int found;

            sbaddr[0] = hash[0] = 0;
            sscanf(s + 4, "%s %s %*s %s %s %s", sessid, sbaddr, hash, arg1, arg2);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            found = msn_clist_update(&msn.FL, arg1, nick, -1, -1, -1);
            if (found > 0) draw_lst(1);
            if (found == -1 && msn.BLP == 'B') {
                UNLOCK(&msn.lock);
            } else {
                msg(C_MSG, "%s <%s> rings\n", nick, arg1);
                LOCK(&SX);
                sprintf(nf, "%s rings\n", nick);
                unotify(nf, SND_RING);
                sb = SL.head;
                found = 0;
                if (sb != NULL) do {
                    /* look for inactive switchboard that can be used again:
                            must have zero users
                            owner must be the same
                            must not have become multiuser */
                    if (sb->PL.count == 0 && strcmp(sb->master, arg1) == 0 && !sb->multi) {
                        found = 1;
                        break;
                    }
                    sb = sb->next;
                } while (sb != SL.head);
                if (!found) sb = msn_sblist_add(&SL, sbaddr, hash, arg1, nick, 1, sessid);
                else {
                    /* reuse inactive switchboard */
                    if (sb->thrid != 1) {
                        pthread_cancel(sb->thrid);
                        pthread_join(sb->thrid, NULL);
                    }
                    strcpy(sb->sbaddr, sbaddr);
                    strcpy(sb->hash, hash);
                    sb->called = 1;
                    strcpy(sb->sessid, sessid);
                }

                draw_sb(sb, 1);
                UNLOCK(&SX);
                UNLOCK(&msn.lock);
                pthread_create(&sb->thrid, NULL, msn_sbdaemon, (void *) sb);
    /*            pthread_detach(sb->thrid);*/
            }
        } else if (is3(com, "OUT")) {

        /* server disconnects user */

            sscanf(s + 4, "%s", arg1);
            if (is3(arg1, "OTH"))
                msg(C_ERR, "OUT: You are logging in from other location\n");
            else if (is3(arg1, "SSD")) msg(C_ERR, "OUT: Server is going down\n");
            else msg(C_DBG, "%s", s);
            if (r == 5) msg(C_MSG, "Disconnected\n");
            break;
        } else

        /* error code or unhandled command */

            if (!scan_error(com)) msg(C_DBG, "<<< %s", s);
    }
    pthread_cleanup_pop(0);
    logout_cleanup((void *) r);
    return NULL;
}

int do_login()
{
    int r;
    char hash[SXL];
#ifdef MSNP8
    char dalogin[SXL], ticket[SXL];
#endif
    char udir[SML];

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

#ifdef MSNP8
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
    
#else
    if ((r = msn_login_md5(msn.nfd, nftid(), msn.pass, hash, msn.nick)) != 0) {
        msg(C_ERR, "%s\n", msn_ios_str(r));
        return -3;
    }
#endif
    draw_status(0);
    msg(C_MSG, "Login successful\n");
    
    /* create user data dir */
    sprintf(udir, "%s/%s", Config.cfgdir, msn.login);
    mkdir(udir, 0700);
    
    pthread_create(&msn.thrid, NULL, msn_ndaemon, (void *) NULL);
    pthread_detach(msn.thrid);

    /* initial status to log in */
    LOCK(&msn.lock);
    msn_chg(msn.nfd, nftid(), Config.initial_status);
    UNLOCK(&msn.lock);

#ifndef MSNP8
    msn_cvr(msn.nfd, nftid, Config.cvr);
#endif

    /* sync lists */
    read_syn_cache_hdr();
    
    msn_syn(msn.nfd, nftid(), syn_cache.ver);

    return 0;
}

void sendstr(char *s)
{
    Write(msn.nfd, s, strlen(s));
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
                    a[i]->gid, i+1, count, a[i]->nick, a[i]->login);
        else
            msg2(C_MNU, "(%d/%d) %s <%s>", i+1, count, a[i]->nick, a[i]->login);
        if (c == ' ' || c == '\r') break;
        switch (c = getch()) {
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
                    msg(C_NORMAL, "%2d. [%d] %s <%s>\n", j + 1, a[j]->gid, a[j]->nick, a[j]->login);
                else for (j = 0; j < count; j++)
                    msg(C_NORMAL, "%2d. %s <%s>\n", j + 1, a[j]->nick, a[j]->login);
                break;
            case 'q':
                msg(C_NORMAL, "NICK: %s\n", a[i]->nick);
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

    msg2(C_MNU, "(F)orward  (R)everse  (A)llow  (B)lock  (G)roup");
    switch (tolower(getch())) {
        case 'f':
            if (menu_clist(&msn.lock, &msn.FL, &p, 1, NULL) >= 0) {
                LOCK(&msn.lock);
                inBL = msn_clist_find(&msn.BL, p.login) != NULL;
                inAL = msn_clist_find(&msn.AL, p.login) != NULL;
                inFL = msn_clist_gcount(&msn.FL, p.login);
                UNLOCK(&msn.lock);
                msg2(C_MNU, "(B)lock  (R)emove  (U)nblock  Re(n)ame  (C)opy  (M)ove  (I)nvite");
                switch (tolower(getch())) {
                    case 'r':
                        msn_rem(msn.nfd, nftid(), 'F', p.login, p.gid);
                        if (inAL && inFL == 1) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                        break;
                    case 'b':
                        if (inBL)
                            msg(C_ERR, "%s <%s> is already blocked\n", p.nick, p.login);
                        else {
                            if (inAL) msn_rem(msn.nfd, nftid(), 'A', p.login, -1);
                            msn_add(msn.nfd, nftid(), 'B', p.login, -1);
                        }
                        break;
                    case 'u':
                        if (!inBL)
                            msg(C_ERR, "%s <%s> is not blocked\n", p.nick, p.login);
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
                    case 'r':
                        msn_rmg(msn.nfd, nftid(), pg.gid);
                        break;
                    case 'n':
                        menu_change_gname(&pg);
                        break;
                }
            }
            break;
    }
}

void menu_msn_options()
{
    int c;

    msg2(C_MNU, "(R)everse list prompting  (A)ll other users");
    switch (tolower(getch())) {
        case 'r':
            msg2(C_MNU, "(A)lways  (N)ever  (D)isplay");
            c = toupper(getch());
            if (c == 'A' || c == 'N') msn_gtc(msn.nfd, nftid(), (char) c);
            if (c == 'D')
                msg(C_NORMAL, "Prompt when other users add you: %s\n", msn.GTC == 'N'? "NEVER": "ALWAYS");
            break;
        case 'a':
            msg2(C_MNU, "(A)llow  (B)lock  (D)isplay");
            c = toupper(getch());
            if (c == 'A' || c == 'B') msn_blp(msn.nfd, nftid(), (char) c);
            if (c == 'D')
                msg(C_NORMAL, "All other users: %s\n", msn.BLP == 'B'? "BLOCKED": "ALLOWED");
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
        case 'c':
            menu_add_contact();
            break;
        case 'g':
            menu_add_group();
    }
}


int log_in()
{
    int r;
    if (msn.nfd != -1) {
        msg(C_ERR, "You are already logged in\n");
        return 0;
    }
    if (!(r = get_string(C_EBX, 0, "Login as: ", msn.login))) return 0;
    if (r == 1) {
        if (strchr(msn.login, '@') == NULL) strcat(msn.login, "@hotmail.com");
        strcpy(msn.nick, msn.login);
        msn.pass[0] = 0;
        draw_status(1);
    }
    if (!get_string(C_EBX, 1, "Password: ", msn.pass)) return 0;
    msg2(C_MNU, "Logging in ...");
    
    while ((r = do_login()) == 1);
    if (r == 0) return 0;
    msn.nfd = -1;
    
    msg(C_ERR, "Failed to connect to server\n");
    return -1;
}

void log_out(int panic)
{
    int retcode;
    struct timespec timeout;
            

    if (msn.nfd == -1) return;
    sboard_leave_all(panic);
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
    } else sleep(5);
    if (panic || (retcode != 0 && msn.nfd != -1)) pthread_cancel(msn.thrid);
}

void show_mbstatus()
{
    LOCK(&msn.lock);
    msg(C_NORMAL, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    UNLOCK(&msn.lock);
}

void sb_blah()
{
    msn_sboard_t *sb;
    char *sp, s[SML];

    LOCK(&msn.lock);
    LOCK(&SX);
    sb = SL.head;
    if (sb != NULL && strlen(sb->input) > 0 && sb->cantype) {
        strcpy(s, sb->input);
        sp = s;
        pthread_setspecific(key_sboard, (void *) sb);
        if (sp[0] == '/') {
            if (sp[1] == '/') sp++;
            else if (sp[1] == ' ') sp += 2; /* secret typing */
            else {
                /* special command */
                char cmd[SML], args[SML];

                eb_history_add(&SL.head->EB, SL.head->EB.text, SL.head->EB.sl);
                eb_settext(&SL.head->EB, SP);
                draw_sbebox(SL.head, 1);

                cmd[0] = args[0] = 0;
                sscanf(sp, "%s %[^\n]", cmd, args);
                if (strcmp(cmd, "/flash") == 0)
                /* easter egg! */
                    flash();
                else if (strcmp(cmd, "/send") == 0) {
                /* send raw command to server */
                    sprintf(s, "%s\r\n", args);
                    Write(sb->sfd, s, strlen(s));
                } else if ((strcmp(cmd, "/invite") == 0) || (strcmp(cmd, "/i") == 0)) {
                    msn_cal(sb->sfd, sb->tid++, args);
                } else if (strcmp(cmd, "/spoof") == 0) {
                /* make server think somebody else is typing! */
                    msn_msg_typenotif(sb->sfd, sb->tid++, args);
                } else if (strcmp(cmd, "/file") == 0) {
                    struct stat st;
                    unsigned int cookie;
                    if (Config.msnftpd == 1) {
                        if (stat(args, &st) == 0) {
                            cookie = lrand48();
                            xfl_add_file(&XL, XS_INVITE, msn.login, SP, 0, cookie, sb, args, st.st_size);
                            msn_msg_finvite(sb->sfd, sb->tid++, cookie, args, st.st_size);
                        } else msg(C_ERR, "Could not stat file '%s'\n", args);
                    } else msg(C_ERR, "Cannot send file: MSNFTP server is not running\n");
                } else msg(C_ERR, "Invalid command\n");
                UNLOCK(&SX);
                UNLOCK(&msn.lock);
                return;
            }
        }
        if (sb->PL.count == 0 && sb->master[0]) { /* call other party again */
            msn_cal(sb->sfd, sb->tid++, sb->master);
            sb->called = 0;
            dlg(C_NORMAL, "Calling other party again, please wait...\n");
            UNLOCK(&SX);
            UNLOCK(&msn.lock);
            return;
        }
        msn_msg_text(sb->sfd, sb->tid++, sp);
        dlg(C_DBG, "%s says:\n", msn.nick);
        dlg(C_MSG, "%s\n", sp);
        eb_history_add(&SL.head->EB, SL.head->EB.text, SL.head->EB.sl);
        eb_settext(&SL.head->EB, SP);
        draw_sbebox(SL.head, 1);
    }
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
}

void sb_type(int c)
{
    msn_sboard_t *sb;
    int command;
    time_t now;

    LOCK(&msn.lock);
    LOCK(&SX);
    sb = SL.head;
    if (sb != NULL && sb->cantype) {
        time(&now);
        command = (sb->input[0] == '/' &&
                    ((sb->input[1] && sb->input[1] != '/') || (!sb->input[1] && c != '/'))) ||
                  (!sb->input[0] && c == '/');
        if (!command && now - sb->tm_last_char > Config.time_user_types) {
            sb->tm_last_char = now;
            if (sb->thrid != -1)
                msn_msg_typenotif(sb->sfd, sb->tid++, msn.login);
            if (sb->PL.count == 0 && sb->master[0]) { /* call other party again */
                msn_cal(sb->sfd, sb->tid++, sb->master);
                sb->called = 0;
            }
        }
        eb_keydown(&sb->EB, c);
        draw_sbebox(sb, 1);
    } 
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
}

void sboard_leave_all(int panic)
{
    int i, c = 0;
    msn_sboard_t *s;
    struct timespec timeout;
    
    if (panic == 0) LOCK(&SX);
    s = SL.head;
    if (s != NULL) {
        c = SL.count;
        SL.out_count = c;
        for (i = 0; i < c; i++, s = s->next) {
            if (s->sfd != -1) msn_out(s->sfd);
        }
    }
    if (panic == 0) {
        if (c > 0) {
            timeout = nowplus(5);
            pthread_cond_timedwait(&SL.out_cond, &SX, &timeout);
        }
        UNLOCK(&SX);
    } else {
        if (c > 0) sleep(5);
    }
}

int sboard_leave()
{
    int r = 1;
    LOCK(&SX);
    if (SL.head != NULL && SL.head->sfd != -1) msn_out(SL.head->sfd);
    else r = 0;
    UNLOCK(&SX);
    return r;
}

int _sboard_kill()
{
    int r;
    LOCK(&SX);
    if ((r = msn_sblist_rem(&SL))) draw_sb(SL.head, 1);
    UNLOCK(&SX);
    return r;
}

int sboard_kill()
{
    if (sboard_leave()) {
        struct timespec timeout;
        LOCK(&SX);
        SL.out_count = 1;
        timeout = nowplus(1);
        pthread_cond_timedwait(&SL.out_cond, &SX, &timeout);
        UNLOCK(&SX);
    }
    return _sboard_kill();;
}


void sboard_next()
{
    LOCK(&SX);
    if (SL.head != NULL) SL.head = SL.head->next;
    draw_sb(SL.head, 1);
    UNLOCK(&SX);
}

void sboard_prev()
{
    LOCK(&SX);
    if (SL.head != NULL) SL.head = SL.head->prev;
    draw_sb(SL.head, 1);
    UNLOCK(&SX);
}

void sboard_next_pending()
{
    msn_sboard_t *s;
    int i, c;
    LOCK(&SX);
    s = SL.head; c = SL.count;
    if (s != NULL) {
        for (i = 0; i < c; i++, s = s->next)
            if (s->pending) break;
        if (s != SL.head) {
            SL.head = s;
            draw_sb(SL.head, 1);
        }
    }
    UNLOCK(&SX);
}

void set_flskip(int delta)
{
    int total;
    LOCK(&msn.lock);
    total = msn.FL.count + msn.GL.count;
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
    scr_event(SCR_RMSG, &w_msg, 0, 1);
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

void redraw_screen()
{
    LOCK(&msn.lock);
    LOCK(&SX);
      erase();
      refresh();
/*      redrawwin(curscr);*/
      draw_all(); 
      msg2(C_MNU, copyright_str);
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
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
        case -(KEY_F(7)): set_plskip(-1); break;
        case -(KEY_F(8)): set_plskip(1); break;
        case '\r': sb_blah(); break;
        default: if (c > 0) sb_type(c);
    }            
}

void print_usage(char *argv0)
{
    fprintf(stderr, "%s\n"
                    "USAGE:\n"
                    "\t%s [[-<option><optval>] ...]\n\n"
                    "OPTIONS:\n"
                    "-h\t\t\tthis help text\n"
                    "-w\t\t\tcreate the default configuration files/dirs and\n"
                    "\t\t\twrite ~/.gtmess/config\n"
                    "\t\t\tfrom command line -O flags that follow\n"
                    "-O<variable>=<value>\tset config var\n"
                    "\n"
                    "config vars, <n>=int, <s>=str, <b>=bool(1/0), []=default\n"
                    "cert_prompt=<b>\t\tprompt on unverified certificates (SSL) [1]\n"
                    "colors=<n>\t\tcolor mode [0]\n\t\t\t<n> = 0 for auto, 1 for color, 2 for mono\n"
                    "common_name_prompt=<b>\tprompt on name mismatch (SSL) [1]\n"
                    "console_encoding=<s>\tconsole encoding (iconv -l) [ISO_8859-7]\n"
                    "cvr=<s>\t\t\tconsumer versioning (CVR) string\n"
                    "initial_status=<n>\tinitial status [1], 0 <= <n> <= 8 for\n"
                    "\t\t\tOffline, Appear_Offline, Online, Idle, Away,\n"
                    "\t\t\tBusy, Be_Right_Back, On_the_Phone, Out_to_Lunch\n"
                    "login=<s>\t\tdefault login account\n"
                    "log_traffic=<b>\t\tlog network traffic [0]\n"
                    "msnftpd=<b>\t\tenable msnftp server [1]\n"
                    "online_only=<b>\t\tshow only online contacts [0]\n"
                    "password=<s>\t\tdefault password\n"
                    "popup=<b>\t\tenable/disable popup notification window [1]\n"
                    "server=<s>[:<n>]\tdispatch/notification server (optional port)\n"
                    "syn_cache=<b>\t\tcache contact/group lists [1]\n"
                    "sound=<n>\t\tsound mode [2]\n"
                    "\t\t\t<n> = 0 for no sound, 1 for beep, 2 for sound effects\n"
                    "time_user_types=<n>\tinterval of typing notifications [%d]\n"
                    "\n"
                    "EXAMPLE:\n"
                    "\t%s -Ologin=george -Opassword=secret123 -Oinitial_status=2\n",
             copyright_str, argv0, ConfigTbl[4].ival, argv0);
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
                if (Config.cfg_fp != NULL) fprintf(Config.cfg_fp, "%s=%s\n", variable, e->sval);
                return 0;
            } else { /* integer */
                if (sscanf(eq+1, "%d", &ival) == 1) {
                    if (ival < e->min) {
                        fprintf(stderr, "parse_cfg_entry(): value too small, %d assumed\n", e->min);
                        ival = e->min;
                    } else if (ival > e->max) {
                        fprintf(stderr, "parse_cfg_entry(): value too big, %d assumed\n", e->min);
                        ival = e->max;
                    }
                    e->ival = ival;
                    if (Config.cfg_fp != NULL) fprintf(Config.cfg_fp, "%s=%d\n", variable, ival);
                    return 0;
                } else fprintf(stderr, "parse_cfg_entry(): integer format expected (%s=?)\n", variable);
            }
        } else fprintf(stderr, "parse_cfg_entry(): invalid variable name `%s'\n", variable);
    } else fprintf(stderr, "parse_cfg_entry(): wrong format (<variable>=<value> expected)\n");
    return -1;
}

void parse_config(int argc, char **argv)
{
    char **arg;
    char tmp[SXL], *nl;
    
    sprintf(tmp, "%s/config", Config.cfgdir);
    Config.cfg_fp = fopen(tmp, "r");
    if (Config.cfg_fp != NULL) {
        while (fgets(tmp, SXL, Config.cfg_fp) != NULL) {
            if (tmp[0] == '#') continue;
            if ((nl = strchr(tmp, '\n')) != NULL) *nl = 0;
            parse_cfg_entry(tmp);
        }
        fclose(Config.cfg_fp);
        Config.cfg_fp = NULL;
    }
    
    for (arg = argv + 1; --argc; arg++) {
        if (arg[0][0] == '-') switch (arg[0][1]) {
            case 'h':
                print_usage(argv[0]);
                _exit(0);
                break;
            case 'w':
                mkdir(Config.cfgdir, 0700);
                sprintf(tmp, "%s/received", Config.cfgdir);
                mkdir(tmp, 0700);
                sprintf(tmp, "%s/notify.pip", Config.cfgdir);
                mkfifo(tmp, 0600);
                sprintf(tmp, "%s/config", Config.cfgdir);
                Config.cfg_fp = fopen(tmp, "w");
                if (Config.cfg_fp == NULL) fprintf(stderr, "cannot open config file for writing\n");
                break;
            case 'O':
                if (parse_cfg_entry(&arg[0][2]) != 0) _exit(1);
                break;
            default:
                fprintf(stderr, "unknown option: %c (-h for help)\n", arg[0][1]);
                _exit(1);
        }
    }
    if (Config.cfg_fp != NULL) fclose(Config.cfg_fp);
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
#ifdef HOME
    sprintf(Config.ca_list, "%s/root.pem", Config.cfgdir);
#else
    sprintf(Config.ca_list, "%s/%s/root.pem", DATADIR, PACKAGE);
#endif
    
    if (Config.login[0] && strchr(Config.login, '@') == NULL) strcat(Config.login, "@hotmail.com");
    strcpy(msn.notaddr, Config.server);
    strcpy(msn.login, Config.login);
    strcpy(msn.pass, Config.password);
}

void get_home_dir(char *dest)
{
    char *s;
    if ((s = getenv("HOME")) != NULL)
        strcpy(dest, s);
    else strcpy(dest, ".");
}

int main(int argc, char **argv)
{
    int c, paste;
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, gtmess_exit_panic);
    
    sprintf(copyright_str, "gtmess, version %s (%s), (c) 2002-2003 by George M. Tzoumas", VERSION, VDATE);
    
    srand48(time(NULL));
    
    pthread_mutex_init(&nftid_lock, NULL);
    
    get_home_dir(Config.cfgdir);
    strcat(Config.cfgdir, "/.gtmess");
    
    parse_config(argc, argv);
    config_init();
    
    
    msn_ic = iconv_open(Config.console_encoding, "UTF-8");
    pthread_mutex_init(&msn.lock, NULL);
    msn_init(&msn);
    pthread_mutex_init(&SX, NULL);
    SL.head = SL.start = NULL; SL.count = 0;
    SL.out_count = 0;
    pthread_cond_init(&SL.out_cond, NULL);
    
    XL.head = XL.tail = XL.cur = NULL; XL.count = 0;

    if (Config.log_traffic) flog = fopen("msn.log", "a");

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
        "(c) 2002-2003 by George M. Tzoumas\n"
        "gtmess is free software, covered by the\nGNU General Public License.\n"
        "There is absolutely no warranty for gtmess.\n\n", VERSION, VDATE);

    msg2(C_MNU, "Welcome to gtmess");

    paste = 0;
    while (1) {
        if (paste == 0) c = getch();
        else paste = 0;
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
                LOCK(&WL); 
                wvis ^= 1; 
                UNLOCK(&WL); 
                LOCK(&SX);
                draw_sb(SL.head, 1);
                UNLOCK(&SX);
                break; 
            } 
            case KEY_F(5): set_msgskip(-1); break;
            case KEY_F(6): set_msgskip(1); break;
            case KEY_F(7): set_flskip(-1); break;
            case KEY_F(8): set_flskip(1); break;
            case 12: redraw_screen(); break; /* ^L */
            case 27: /* escape mode */
                msg2(C_MNU, "ESC: (A)dd (C)onn/(D)isconn (S)tat (L)st (N)am (O)pt (M)ail (P)ng (I)nv");
                c = tolower(getch());
/*                msg2(C_MNU, copyright_str);*/
/*                usleep(1000);*/
                if (c == '0') {
                    c = KEY_F(10);
                    paste = 1;
                } else if (c >= '1' && c <= '9') {
                    int k = c - '0';
                    c = KEY_F(k);
                    paste = 1;
                } else if (c == 'c') {
                    log_in();
                    LOCK(&SX);
                    draw_sb(SL.head, 0);
                    UNLOCK(&SX);
                } else if (strchr("sloadnmpi/", c) != NULL) {
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
                        case 'o': menu_msn_options(); break;
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
