/*
 *    sboard.c
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

#include<curses.h>
#include<pthread.h>
#include<stdlib.h>
#include<iconv.h>
#include<errno.h>
#include<sys/stat.h>
#include<string.h>
#include<time.h>

#include"sboard.h"
#include"xfer.h"
#include"unotif.h"
#include"../inty/inty.h"
#include"gtmess.h"

pthread_mutex_t SX; /* switchboard mutex */
msn_sblist_t SL; /* switchboard */
pthread_key_t key_sboard;
xqueue_t q_sb_req; /* new-sb requests */

time_t dlg_time = 0;

extern char MyIP[];

void sboard_leave_all(int panic)
{
    int i, c = 0, o = 0;
    msn_sboard_t *s;
    struct timespec timeout;
    
    if (panic == 0) LOCK(&SX);
    s = SL.head;
    if (s != NULL) {
        c = SL.count;
        for (i = 0; i < c; i++, s = s->next) if (s->sfd != -1) o++;
        SL.out_count = o;
        s = SL.head;
        for (i = 0; i < c; i++, s = s->next) {
            if (s->sfd != -1) msn_out(s->sfd);
        }
    }
    if (panic == 0) {
        if (o > 0) {
            timeout = nowplus(5);
            pthread_cond_timedwait(&SL.out_cond, &SX, &timeout);
        }
        UNLOCK(&SX);
    } else {
        if (o > 0) sleep(1);
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
    if (SL.head != NULL) {
        SL.head = SL.head->next;
        if (SL.head->pending) SL.head->pending = 0;
        draw_sb(SL.head, 1);
    }
    UNLOCK(&SX);
}

void sboard_prev()
{
    LOCK(&SX);
    if (SL.head != NULL) {
        SL.head = SL.head->prev;
        if (SL.head->pending) SL.head->pending = 0;
        draw_sb(SL.head, 1);
    }
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
        s->pending = 0;
    }
    UNLOCK(&SX);
}

void sb_blah()
{
    msn_sboard_t *sb;
    char *sp, s[SXL];

    LOCK(&msn.lock);
    LOCK(&SX);
    sb = SL.head;
    if (sb != NULL && strlen(sb->EB.text) > 0) {
        if (sb->sfd > -1) {
            sp = sb->EB.text;
            pthread_setspecific(key_sboard, (void *) sb);
            if (sp[0] == '/') {
                if (sp[1] == '/') sp++;
                else if (sp[1] == ' ') sp += 2; /* secret typing */
                else {
                    /* special command */
                    char cmd[SXL], args[SXL];

                    eb_history_add(&SL.head->EB, SL.head->EB.text, strlen(SL.head->EB.text));

                    cmd[0] = args[0] = 0;
                    sscanf(sp, "%s %[^\n]", cmd, args);
                    
                    eb_settext(&SL.head->EB, ZS);
                    draw_sbebox(SL.head, 1);
                    
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
                    /* make some clients think somebody else is typing! */
                        msn_msg_typenotif(sb->sfd, sb->tid++, args);
                    } else if (strcmp(cmd, "/beep") == 0) {
                        msn_msg_gtmess(sb->sfd, sb->tid++, "BEEP", ZS);
                    } else if (strcmp(cmd, "/gtmess") == 0) {
                        msn_msg_gtmess(sb->sfd, sb->tid++, "GTMESS", ZS);
                    } else if (strcmp(cmd, "/msg") == 0) {
                        msn_msg_gtmess(sb->sfd, sb->tid++, "MSG", args);
                        msg(C_DBG, "%s says:\n", getnick(msn.login, msn.nick, Config.aliases));
                        msgn(C_MSG, strlen(args)+2, "%s\n", args);
                    } else if (strcmp(cmd, "/dlg") == 0) {
                        msn_msg_gtmess(sb->sfd, sb->tid++, "DLG", args);
                        dlg(C_DBG, "%s says:\n", getnick(msn.login, msn.nick, Config.aliases));
                        dlgn(C_MSG, strlen(args)+2, "%s\n", args);
                    } else if (strcmp(cmd, "/file") == 0) {
                        struct stat st;
                        unsigned int cookie;
                        if (Config.msnftpd > 0) {
                            if (stat(args, &st) == 0) {
                                cookie = lrand48();
                                xfl_add_file(&XL, XS_INVITE, msn.login, ZS, 0, cookie, sb, args, st.st_size);
                                msn_msg_finvite(sb->sfd, sb->tid++, cookie, args, st.st_size);
                            } else msg(C_ERR, "Could not stat file '%s'\n", args);
                        } else msg(C_ERR, "Cannot send file: MSNFTP server is not running\n");
                    } else msg(C_ERR, "Invalid command\n");
                    UNLOCK(&SX);
                    UNLOCK(&msn.lock);
                    return;
                }
            }
            if (sb->PL.count == 0) {
                if (sb->master[0]) { /* call other party again */
                    msn_cal(sb->sfd, sb->tid++, sb->master);
                    sb->called = 0;
                    dlg(C_NORMAL, "Calling other party again, please wait...\n");
                    UNLOCK(&SX);
                    UNLOCK(&msn.lock);
                    return;
                } else {
                    msg(C_ERR, "No switchboard participants\n");
                    UNLOCK(&SX);
                    UNLOCK(&msn.lock);
                    return;
                }
            }
            msn_msg_text(sb->sfd, sb->tid++, sp);
            dlg(C_DBG, "%s says:\n", getnick(msn.login, msn.nick, Config.aliases));
            dlgn(C_MSG, strlen(sp)+2, "%s\n", sp);
            eb_history_add(&SL.head->EB, SL.head->EB.text, strlen(SL.head->EB.text));
            eb_settext(&SL.head->EB, ZS);
            draw_sbebox(SL.head, 1);
        } else  if (sb->multi == 0) { /* reconnect only if not multiuser */
            char s[SML];

            sprintf(s, "XFR %u SB\r\n", nftid());
            /* enqueue the request; normally the server will respond in
               the same order so we do not have to do any request tracking */
            queue_put(&q_sb_req, 0, sb, 0);
            Write(msn.nfd, s, strlen(s));
        }
    }
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
}

int sb_type(int c)
{
    msn_sboard_t *sb;
    int command;
    time_t now;
    int res;

    LOCK(&msn.lock);
    LOCK(&SX);
    sb = SL.head;
    if (sb != NULL) {
            time(&now);
            command = (sb->EB.text[0] == '/' &&
                        ((sb->EB.text[1] && sb->EB.text[1] != '/') || (!sb->EB.text[1] && c != '/'))) ||
                      (!sb->EB.text[0] && c == '/');
            if (!command && now - sb->tm_last_char > Config.time_user_types) {
                sb->tm_last_char = now;
                if (sb->thrid != -1 && sb->PL.count > 0)
                    msn_msg_typenotif(sb->sfd, sb->tid++, msn.login);
                if (sb->sfd > -1) {
                    if (sb->PL.count == 0 && sb->master[0]) { /* call other party again */
                        msn_cal(sb->sfd, sb->tid++, sb->master);
                        sb->called = 0;
                    }
                } else if (sb->multi == 0) { /* reconnect only if not multiuser */
                    char s[SML];

                    sprintf(s, "XFR %u SB\r\n", nftid());
                    /* enqueue the request; normally the server will respond in
                       the same order so we do not have to do any request tracking */
                    queue_put(&q_sb_req, 0, sb, 0);
                    Write(msn.nfd, s, strlen(s));
                }
            }
            res = eb_keydown(&sb->EB, c);
            draw_sbebox(sb, 1);
    }
    UNLOCK(&SX);
    UNLOCK(&msn.lock);
    return res;
}

void fcopy(FILE *s, FILE *d)
{
    size_t k;
    char buf[4096];

    while ((k = fread(buf, 1, sizeof(buf), s)) > 0) fwrite(buf, 1, k, d);
}

void sb_cleanup(void *arg)
{
    msn_sboard_t *sb = (msn_sboard_t *) arg;

    close(sb->sfd);
    sb->sfd = -1;
    msn_clist_free(&sb->PL);
    sb->thrid = -1;
    sb->tid = 0;
    if (sb->fp_log != NULL) { /* copy temp log file */
        if (sb->master[0]) {
            char tmp[SML];
            FILE *f;
            sprintf(tmp, "%s/%s/log/%s", Config.cfgdir, msn.login, sb->master);
            if ((f = fopen(tmp, "a")) != NULL) {
                rewind(sb->fp_log);
                fcopy(sb->fp_log, f);
                fprintf(f, "\n----------------------------------------\n\n");
                fclose(f);
            }
        }
        fclose(sb->fp_log);
        sb->fp_log = NULL;
        remove(sb->fn_log);
    }
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

int do_connect_sb(char *server)
{
    int sfd;

    if ((sfd = ConnectToServer(server, 1863)) < 0)
        dlg(C_ERR, "%s\n", strerror(errno));
    else dlg(C_DBG, "Connected\n");
    return sfd;
}

void msn_sb_handle_msgbody(char *arg1, char *arg2, char *msgdata, int len)
{
    char contype[SML], nick[SML];
    char *s;
    msn_sboard_t *sb = pthread_getspecific(key_sboard);

    sb->pending = 0;
    if (strchr(arg1, '@') != NULL) url2str(arg2, nick);
    else sprintf(nick, "<%s>", getnick(arg1, arg1, Config.aliases));
    contype[0] = 0;
    s = strafter(msgdata, "Content-Type: ");
    if (s == NULL) {
        dlgn(C_DBG, len+2, "%s\n", msgdata);
        return;
    }
    sscanf(s, "%s", contype);

    if (strstr(contype, "text/plain") != NULL) {
        
    /* user chats */
        char *text;
        
        s = strafter(msgdata, "\r\n\r\n");
        text = (char *) Malloc(len - (s - msgdata) + 1);
        sb->pending = 1;
        
        utf8decode(sb->ic, s, text);
        dlg(C_DBG, "%s says:\n", getnick(arg1, nick, Config.aliases));
        dlgn(C_MSG, len+2, "%s\n", text);
/*        playsound(SND_MSG); */
        free(text);
        
        return;
    }
    
    if (strstr(contype, "text/x-msmsgscontrol") != NULL) {
        
    /* user types */
        time_t now;
        
        time(&now);
        msn_clist_update(&msn.FL, arg1, NULL, -1, -1, now);
        draw_lst(1);
        /* rather tricky! (in order to be drawn *after* time_user_types sec) */
        sched_draw_lst(Config.time_user_types + 1); 
        return;
    }
    
    if (strstr(contype, "text/x-gtmesscontrol") != NULL) {
        
    /* gtmess special message */
        char cmd[SNL], args[SXL];
        
        if (strcmp(Config.gtmesscontrol_ignore, "*all*") == 0) return;
        
        cmd[0] = args[0] = 0;
        if ((s = strafter(msgdata, "Command: ")) != NULL) {
            sscanf(s, "%s", cmd);
            if ((s = strafter(msgdata, "Args: ")) != NULL)
                sscanf(s, "%[^\r\n]s", args);
            
            if (strstr(Config.gtmesscontrol_ignore, cmd) != NULL) return;
            
            if (strcmp(cmd, "BEEP") == 0) {
                msg(C_DBG, "%s beeps\n", nick);
                unotify("BEEP!\n", SND_PENDING);
            } else if (strcmp(cmd, "GTMESS") == 0) msg(C_DBG, "%s is using gtmess\n", nick);
            else if (strcmp(cmd, "MSG") == 0) {
                char *text = strdup(args);
                utf8decode(msn_ic[0], args, text);
                msg(C_DBG, "%s says:\n", getnick(arg1, nick, Config.aliases));
                msgn(C_MSG, len+2, "%s\n", text);
                free(text);
            } else if (strcmp(cmd, "DLG") == 0) {
                char *text = strdup(args);
                utf8decode(sb->ic, args, text);
                sb->pending = 1;
                dlg(C_DBG, "%s says:\n", getnick(arg1, nick, Config.aliases));
                dlgn(C_MSG, len+2, "%s\n", text);
/*                playsound(SND_MSG);   */
                free(text);
            }
        }
        return;
    }
    
    if (strstr(contype, "text/x-msmsgsinvite") != NULL) {
        
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
                    dlg(C_DBG, "%s wants to send you the file '%s', %u bytes long\n", getnick(arg1, nick, Config.aliases), fname, size);
                } else {
                /* reject all other types of invitations */
                    dlgn(C_DBG, len+2, "%s\n", msgdata);
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
                    x->data.file.port = Config.msnftpd;
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
                        dlg(C_DBG, "%s is not sending you '%s' any more\n", getnick(arg1, nick, Config.aliases), x->data.file.fname);
                    else
                        dlg(C_DBG, "%s is not receiving '%s'\n", getnick(arg1, nick, Config.aliases), x->data.file.fname);
                }
                x->status = xs;
            } else {
                x->status = xs;
            }
            UNLOCK(&XX);
        } else {
            dlgn(C_DBG, len+2, "%s\n", msgdata);
        }
        draw_sb(sb, 1);
        return;
    }
    
    if (Config.msg_debug == 2) dlgn(C_DBG, len+2, "%s\n", msgdata);
    else if (Config.msg_debug == 1) dlg(C_DBG, "MSG: %s\n", contype);
    
    if (Config.msg_debug > 0) sb->pending = 1;
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
    time_t tm;

    pthread_setspecific(key_sboard, arg);
    LOCK(&SX);
    tm = time(NULL);
    s[0] = 0;
    ctime_r(&tm, s);
    dlg(C_NORMAL, "Switchboard session begin on %s", s);
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
        pthread_testcancel();
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
                
                if (sb->pending) {
                    if (Config.msg_notify > 0) {
                        if ((Config.msg_notify == 1 && msn.status == MS_IDL) ||
                            (Config.msg_notify > 1  && difftime(time(NULL), dlg_time) >= Config.msg_notify)) {
                                time(&dlg_time);
                                unotify("New message\n" , SND_PENDING);
                        }
                    } else if (SL.head != sb) playsound(SND_PENDING);
                    if (SL.head == sb) sb->pending = 0;
                }
                
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
            if (sb->PL.count == 1) {
                strcpy(sb->master, arg1);
                strcpy(sb->mnick, nick);
            }
            draw_plst(sb, 0);
            dlg(C_DBG, "%s <%s> has joined the conversation\n", getnick(arg1, nick, Config.aliases), arg1);
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
            dlg(C_DBG, "%d. %s <%s>\n", index, getnick(arg1, nick, Config.aliases), arg1);
            UNLOCK(&SX);
        } else if (is3(com, "USR")) {
            LOCK(&SX);
            dlg(C_DBG, "Ready to invite other users\n");
            if (sb->master[0]) /* auto invite previous user */
                msn_cal(sb->sfd, sb->tid++, sb->master);
            UNLOCK(&SX);
        } else if (is3(com, "ANS")) {

        /* end of initial negotiation */

            LOCK(&SX);
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

            sscanf(s + 4, "%s", arg1);
            LOCK(&msn.lock);
            LOCK(&SX);
            msn_clist_rem(&sb->PL, arg1, -1);
	    /* gtmess is a GreaT MESS of source code! ;-) [egg] */
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
                       "Message-id: %u, Last-id-sent: %u\n", id, sb->tid-1);
            UNLOCK(&SX);
        } else {

        /* error code */

            LOCK(&SX);
            sb->pending = 1;
            if (!scan_error_sb(com))
                dlgn(C_DBG, strlen(s)+6, "<<< %s\n", s);
            UNLOCK(&SX);
        }
    }
    tm = time(NULL);
    s[0] = 0;
    ctime_r(&tm, s);
    dlg(C_ERR, "Disconnected from switchboard on %s", s);
/*    sb->pending = 1;*/
/*    snd_play(SND_PENDING);*/
    pthread_cleanup_pop(0);
    LOCK(&SX);
    sb_cleanup(arg);
    draw_plst(sb, 0);
    if (SL.out_count > 0) SL.out_count--;
    if (SL.out_count == 0) pthread_cond_signal(&SL.out_cond);
    UNLOCK(&SX);
    pthread_detach(pthread_self());
    return NULL;
}

void sboard_openlog(msn_sboard_t *p)
{
    sprintf(p->fn_log, "%s/%s/log/%s", Config.cfgdir, msn.login, p->hash);
    if ((Config.log_console & 1) == 1) p->fp_log = fopen(p->fn_log, "w+");
    else p->fp_log = NULL;
}

msn_sboard_t *msn_sblist_add(msn_sblist_t *q, char *sbaddr, char *hash,
                             char *master, char *mnick, int called, char *sessid)
{
    msn_sboard_t *p;

    p = (msn_sboard_t *) Malloc(sizeof(msn_sboard_t));
    strcpy(p->sbaddr, sbaddr);
    strcpy(p->hash, hash);
    p->called = called;
    sboard_openlog(p);

    p->sessid[0] = 0;
    if (sessid != NULL) strcpy(p->sessid, sessid);
    p->PL.head = NULL; p->PL.count = 0;
    strcpy(p->master, master);
    strcpy(p->mnick, mnick);
    p->multi = 0;

    p->w_dlg.w = W_DLG_W;
    p->w_dlg.h = W_DLG_H;
    p->w_dlg_top = 0;
    p->w_dlg.wh = newpad(W_DLG_LBUF, p->w_dlg.w);
/*    p->w_dlg.wh = newwin(p->w_dlg.h, p->w_dlg.w, 0, 0);*/
    scrollok(p->w_dlg.wh, TRUE);
    p->w_dlg.x = W_DLG_X;
    p->w_dlg.y = W_DLG_Y;
    pthread_mutex_init(&p->w_dlg.lock, NULL);
    p->w_prt.w = W_PRT_W;
    p->w_prt.h = W_PRT_H;
    p->w_prt.wh = newwin(p->w_prt.h, p->w_prt.w, 0, 0);
    scrollok(p->w_prt.wh, TRUE);
    p->w_prt.x = W_PRT_X;
    p->w_prt.y = W_PRT_Y;
    pthread_mutex_init(&p->w_prt.lock, NULL);

    if (!utf8_mode) 
        p->ic = iconv_open(Config.console_encoding, "UTF-8");
    else p->ic = (iconv_t) -1;
    p->tid = 0;
    p->sfd = -1;
    p->thrid = -1;
    eb_init(&p->EB, SXL-1, w_msg.w);
    p->EB.grow = 1;
    p->EB.utf8 = utf8_mode;
    p->EB.history = 1;
/*    eb_settext(&p->EB, ZS);*/
    p->pending = 0;
    p->tm_last_char = 0;
    p->plskip = 0;
    p->dlg_now = 0;

    if (q->head == NULL) {
        /* empty list */
        q->head = p;
        p->next = p->prev = p;
        q->start = q->head;
    } else {
        /* not empty: insert right before head */
        q->head->prev->next = p;
        p->next = q->head;
        p->prev = q->head->prev;
        q->head->prev = p;
    }

    q->count++;
    return p;
}

int msn_sblist_rem(msn_sblist_t *q)
{
    msn_sboard_t *p = q->head;
    xfer_t *x;

    if (p == NULL) return 0;
    if (p->thrid != -1) {
/*        close(p->sfd);*/
        pthread_t tmp = p->thrid;
        pthread_cancel(tmp);
        pthread_join(tmp, NULL);
    }
    msn_clist_free(&p->PL);
    delwin(p->w_dlg.wh);
    delwin(p->w_prt.wh);
    pthread_mutex_destroy(&p->w_dlg.lock);
    pthread_mutex_destroy(&p->w_prt.lock);
    if (p->ic != (iconv_t) -1) iconv_close(p->ic);

    p->prev->next = p->next;
    p->next->prev = p->prev;
    if (q->start == p) q->start = q->start->next;
    if (q->start == p) q->start = NULL;
    q->head = p->next;
    if (q->head == p) q->head = NULL; /* only one element */
    eb_free(&p->EB);
    LOCK(&XX);
       for (x = XL.head; x != NULL; x = x->next) if (x->sb == p) {
           x->sb = NULL;
           if (x->status < XS_CONNECT) x->status = XS_ABORT;
       }
    UNLOCK(&XX);
    free(p);
    q->count--;
    return 1;
}
