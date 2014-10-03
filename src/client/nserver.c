/*
 *    nserver.c
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

#include <stdio.h>        
#include <stdlib.h>       
#include <errno.h>        
#include <pthread.h>      
#include <sys/time.h>     
#include <string.h>       

#include "../inty/inty.h" 
#include "gtmess.h"       
#include "unotif.h"       
#include "screen.h"       
#include "sboard.h"       

msn_t msn;

/*syn_hdr_t syn_cache;*/

/*extern FILE *syn_cache_fp;*/
FILE *flog;

extern pthread_cond_t cond_out;
extern struct timeval tv_ping;
extern int show_ping_result;

extern int skip_pcs;

void notif_openlog()
{
    sprintf(msn.fn_log, "%s/%s/log/%s", Config.cfgdir, msn.login, "_nserver.log");
    if ((Config.log_console & 2) == 2) {
        time_t tm = time(NULL);
        char s[SNL];
        
        msn.fp_log = fopen(msn.fn_log, "a");
        
        s[0] = 0;
        ctime_r(&tm, s);
        fprintf(msn.fp_log, "Server log for user <%s> on %s", msn.login, s);
    }  else msn.fp_log = NULL;
}
	
void logout_cleanup(void *r)
{
    LOCK(&msn.lock);
    
    close(msn.nfd);
    msn.nfd = -1;
    msn_glist_free(&msn.GL);
    msn_clist_free(&msn.CL);
    if (msn.fp_log != NULL) {
        time_t tm = time(NULL);
        char s[SNL];
        s[0] = 0;
        ctime_r(&tm, s);
        fprintf(msn.fp_log, "Server log stopped on %s", s);
        fprintf(msn.fp_log, "\n----------------------------------------\n\n");
        fclose(msn.fp_log);
        msn.fp_log = NULL;
    }
    msn_init(&msn);
    
    LOCK(&SX);
    draw_status(0);
    draw_lst(1);
/*    draw_all();*/
    UNLOCK(&SX);
    pthread_cond_signal(&cond_out);
    UNLOCK(&msn.lock);
    
    if (r == (void *) 0 || r == (void *) -2)
        msg(C_ERR, "msn_ndaemon(): disconnected from server\n");
    unotify("You have been signed out\n", SND_LOGOUT);
/*    msg(C_DBG, "msn_ndaemon(): flushed %d bytes\n", buf.len);*/
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

void end_of_sync()
{
    /* end of sync -- last LST/LSG rmember */
    int match_color = C_MSG;
    int ignored, notify, count = 0;
    char name[SML], fn[SML];
    FILE *f;
    int cF = 0, cA = 0, cB = 0, cR = 0, cP = 0; /* counters */
    msn_contact_t *p;

    /* loading per-contact settings */
    sprintf(fn, "%s/%s/per_contact", Config.cfgdir, msn.login);
    f = fopen(fn, "r");
    if (f != NULL) {
        char ver[SNL] = {0};
        fscanf(f, "#GTMESS:%[^:]s:PCS\n", ver);
        if (strcmp(ver, "0.94") < 0) {
            msg(C_ERR, "%s: version mismatch -- delete to recreate with defaults\n", fn);
            skip_pcs = 1;
        } else {
            while (!feof(f))
                if (fscanf(f, "%s\t%d\t%d\n", name, &notify, &ignored) == 3)
                    /* flags = 0, we allow for ALL lists, not just FL */
                    if ((p = msn_clist_find(&msn.CL, 0, name)) != NULL) {
                        p->notify = notify;
                        p->ignored = ignored;
                        count++;
                    }
            fclose(f);
            if (count > 0) msg(C_DBG, "Loaded specific settings for %d contact%c\n", count, (count != 1)? 's': ' ');
        }
    }

    msn.in_syn = 0;
    for (p = msn.CL.head; p != NULL; p = p->next) {
        cF += (p->lflags & msn_FL) == msn_FL;
        cA += (p->lflags & msn_AL) == msn_AL;
        cB += (p->lflags & msn_BL) == msn_BL;
        cR += (p->lflags & msn_RL) == msn_RL;
        cP += (p->lflags & msn_PL) == msn_PL;
    }

    if (cP > 0 || cA + cB < cR) match_color = C_DBG;

    msg(match_color, "FL/AL/BL/RL/PL contacts: %d/%d/%d/%d/%d\n", cF, cA, cB, cR, cP);

    /* activate contact list if no SB windows? */
/*                LOCK(&SX);
    if (SL.count == 0) clmenu = 1;
    UNLOCK(&SX);*/
    draw_lst(1);
/*    write_syn_cache();*/
    /* initial status to log in */
    if (Config.force_nick[0]) msn_prp(msn.nfd, nftid(), Config.force_nick);
    if (msn.psm[0]) msn_uux(msn.nfd, nftid(), msn.psm);
    if (Config.initial_status) msn_chg(msn.nfd, nftid(), Config.initial_status);
}

void msn_handle_msgbody(char *msgdata, int len)
{
    char contype[SML];
    char *s;

    contype[0] = 0;
    s = strafter(msgdata, "Content-Type: ");
    sscanf(s, "%s", contype);

    if (strstr(contype, "text/x-msmsgsinitialmdatanotification") != NULL) {
    /* initial email data notification */
        s = strafter(msgdata, "<IU>");
        sscanf(s, "%d", &msn.inbox);
        s = strafter(msgdata, "<OU>");
        sscanf(s, "%d", &msn.folders);

        msg(C_MSG, "MBOX STATUS: Inbox: %d, Folders: %d\n", msn.inbox, msn.folders);
    } else if (strstr(contype, "text/x-msmsgsemailnotification") != NULL) {
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
    } else if (strstr(contype, "text/x-msmsgsprofile") != NULL) {
        char tmp[SML];
        tmp[0] = 0;
        s = strafter(msgdata, "\nClientIP: ");
        sscanf(s, "%[^\r]", tmp);
        if (strlen(tmp) > 7) {
            Strcpy(MyIP, tmp, SML);
            msg(C_MSG, "External IP: %s\n", MyIP);
        }
        
    /* user profile */
    } else if (strstr(contype, "text/x-msmsgsactivemailnotification") != NULL) {
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
    } else if (strstr(contype, "application/x-msmsgssystemmessage") != NULL) {
        
    /* system message */    
        
        int type = -1, arg1 = -1;
        s = strafter(msgdata, "\nType: "); sscanf(s, "%d", &type);
        s = strafter(msgdata, "\nArg1: "); sscanf(s, "%d", &arg1);
        if (type == 1) msg(C_MSG, "The server is going down for maintenance in %d minutes", arg1);
        else msg(C_DBG, "%s\n", msgdata);
    } else {
        if (Config.msg_debug == 2) msgn(C_DBG, strlen(msgdata)+2, "%s\n", msgdata);
        else if (Config.msg_debug == 1) msg(C_DBG, "MSG: %s\n", contype);
    }
}

char *msn_get_payload(buffer_t *buf, int size)
{
    char *msgdata;
    int r;
    msgdata = (char *) Malloc(size+1);
    r = bfFlushN(buf, msgdata, size);
    bfReadX(buf, msgdata + r, size - r);
    msgdata[size] = 0;
    return msgdata;
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
    int rerrno;

    notif_openlog();
    bfAssign(&buf, msn.nfd);
    pthread_cleanup_push(logout_cleanup, (void *) 0);
    while (1) {
        r = bfParseLine(&buf, s, SXL);
		rerrno = errno;
        if (r == -2) msg(C_ERR, "bfParseLine(): buffer overrun\n");
        else if (r < 0) msg(C_ERR, "bfParseLine(): %s\n", strerror(errno));
        pthread_testcancel();
/*        if (r >= 0) msg("bfParseLine(): parsed %d bytes\n", r);*/
        if (Config.log_traffic && r > 0) {
            struct tm t;
            time_t t1;
            time(&t1);
            localtime_r(&t1, &t);
            fprintf(flog, "%d-%02d-%02d %02d:%02d:%02d> %s", 1900+t.tm_year, 1+t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, s);
            fflush(flog);
        }
		if (r < 0 && rerrno == EINTR) continue;
        if (r <= 0) break;

        com[0] = 0; arg1[0] = 0; arg2[0] = 0;
        msglen = 0; msgdata = NULL;

        /* msgn(C_DBG, strlen(s)+6, ">>> %s\n", s); */

        sscanf(s, "%s", com);
        if (is3(com, "MSG")) {

        /* message */

            if (sscanf(s + 4, "%s %s %d", arg1, arg2, &msglen) == 3) {
                msgdata = msn_get_payload(&buf, msglen);
                if (Config.log_traffic && msglen > 0) {
                    fprintf(flog, "%s\n", msgdata);
                    fflush(flog);
                }
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

            if (msn.in_syn) 
                sscanf(s + 4, "%s", arg2);
            else
                sscanf(s + 4, "%*s %*s %s", arg2);
            LOCK(&msn.lock);
            msn.GTC = arg2[0];
            UNLOCK(&msn.lock);
            msg(C_MSG, "Prompt when other users add you: %s\n", arg2[0] == 'N'? "NEVER": "ALWAYS");
        } else if (is3(com, "BLP")) {

        /* privacy mode */

            if (msn.in_syn) 
                sscanf(s + 4, "%s", arg2);
            else
                sscanf(s + 4, "%*s %*s %s", arg2);
            LOCK(&msn.lock);
            msn.BLP = arg2[0];
            UNLOCK(&msn.lock);
            msg(C_MSG, "All other users: %s\n", arg2[0] == 'B'? "BLOCKED": "ALLOWED");
        } else if (is3(com, "REG")) {

        /* group rename */

            msn_group_t *pg;

            sscanf(s + 4, "%*s %s %s", arg1, arg2);
            LOCK(&msn.lock);
            url2str(arg2, nick);
            if ((pg = msn_glist_find(&msn.GL, arg1)) != NULL) {
                msg(C_MSG, "Group %s renamed to %s\n", pg->name, nick);
                Strcpy(pg->name, nick, SML);
                draw_lst(1);
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "ADG")) {

        /* group add */

            sscanf(s + 4, "%*d %s %s", arg1, arg2);
            url2str(arg1, nick);
            LOCK(&msn.lock);
            msn_glist_add(&msn.GL, arg2, nick);
            draw_lst(1);
            msg(C_MSG, "Added group %s with uuid %s\n", nick, arg2);
            UNLOCK(&msn.lock);
        } else if (is3(com, "RMG")) {

        /* group remove*/

            int nr;

            sscanf(s + 4, "%*d %s", arg1);
            LOCK(&msn.lock);
            msn_glist_rem(&msn.GL, arg1);
            nr = msn_clist_rem_grp(&msn.CL, arg1);
            draw_lst(1);
            msg(C_MSG, "Removed group with %d contacts\n", nr);
            UNLOCK(&msn.lock);

        } else if (is3(com, "LSG")) {

        /* group list */
            msn_group_t *pg;

            sscanf(s + 4, "%s %s", arg1, arg2);
            url2str(arg1, nick);
            LOCK(&msn.lock);
            pg = msn_glist_add(&msn.GL, arg2, nick);
            /*msg(C_DBG, "added group %s %s\n", pg->name, pg->gid);*/
            if (--msn.list_count == 0) end_of_sync();
            UNLOCK(&msn.lock);
        } else if (is3(com, "ILN")) {

        /* initial status of FL entries */

            char stat[SML];

            LOCK(&msn.lock);
            sscanf(s + 4, "%*u %s %s %s", stat, arg1, arg2);
            url2str(arg2, nick);
            if (msn_clist_update(&msn.CL, msn_FL, arg1, nick, msn_stat_id(stat), -1, 0) > 0) {
                draw_lst(1);
            }
            UNLOCK(&msn.lock);
            msg(C_MSG, "%s <%s> is %s\n", getnick2(arg1, nick), arg1, msn_stat_name[msn_stat_id(stat)]);
        } else if (is3(com, "NLN")) {

        /* contact changes to online status */

            char stat[SNL];
            msn_stat_t old = MS_UNK;

            LOCK(&msn.lock);
            sscanf(s + 4, "%s %s %s", stat, arg1, arg2);
            url2str(arg2, nick);
            if ((p = msn_clist_find(&msn.CL, msn_FL, arg1)) != NULL) old = p->status;
            if (msn_clist_update(&msn.CL, msn_FL, arg1, nick, msn_stat_id(stat), -1, 0) > 0) {
                if (can_notif(arg1) && old < MS_NLN) {
                    char nf[SML];
                    sprintf(nf, "%s is %s\n", getnick2(arg1, nick), msn_stat_name[msn_stat_id(stat)]);
                    unotify(nf, SND_ONLINE);
                }
                draw_lst(1);
                msg(C_MSG, "%s <%s> is %s\n", getnick2(arg1, nick), arg1, msn_stat_name[msn_stat_id(stat)]);
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "FLN")) {

        /* contact appears offline */

            LOCK(&msn.lock);
            sscanf(s + 4, "%s", arg1);
            p = msn_clist_find(&msn.CL, msn_FL, arg1);
            if (p != NULL) {
                if (p->status == MS_FLN)
                    msg(C_ERR, "%s <%s> possibly appears offline\n", getnick1c(p), arg1);
                else if (msn_clist_update(&msn.CL, msn_FL, arg1, NULL, MS_FLN, -1, 0) > 0) {
                    draw_lst(1);
                    msg(C_MSG, "%s <%s> is %s\n", getnick1c(p), arg1, msn_stat_name[MS_FLN]);
                    if (can_notif(arg1)) {
                        char nf[SML];
                        sprintf(nf, "%s is Offline\n", getnick1c(p));
                        unotify(nf, SND_OFFLINE);
                    }
                }
            }
            UNLOCK(&msn.lock);
        } else if (is3(com, "REM")) {

        /* list remove */

            char lst[SML];
            int nf;

            lst[0] = 0;
            nf = sscanf(s + 4, "%*u %s %s %s", lst, arg1, arg2);
            switch (lst[0]) {
                case 'F':
                    LOCK(&msn.lock);
                    if (nf == 3) msn_clist_rem(&msn.CL, msn_FL, arg1, arg2);
                    else msn_clist_rem(&msn.CL, msn_FL, arg1, NULL);
                    draw_lst(1);
                    p = msn_clist_findu(&msn.CL, 0, arg1);
                    if (p) {
                        if (nf == 3)
                            msg(C_MSG, "%s <%s> has been removed from group %s\n",
                                getnick1c(p), p->login, msn_glist_findn(&msn.GL, arg2));
                        else msg(C_MSG, "%s <%s> has been removed from your forward list\n",
                                 getnick1c(p), p->login);
                    }
                    UNLOCK(&msn.lock);
                    break;
                case 'A':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.CL, msn_AL, arg1, NULL);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been removed from your allow list\n",  getnick1(arg1), arg1);
                    break;
                case 'B':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.CL, msn_BL, arg1, NULL);
                    draw_lst(1);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been unblocked\n", getnick1(arg1), arg1);
                    break;
                case 'R':
                    /* MSNP: possibly unreachable code */
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.CL, msn_RL, arg1, NULL);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been removed from your reverse list\n", getnick1(arg1), arg1);
                    break;
                case 'P':
                    LOCK(&msn.lock);
                    msn_clist_rem(&msn.CL, msn_PL, arg1, NULL);
                    UNLOCK(&msn.lock);
                    /* msg(C_MSG, "%s <%s> has been removed from your pending list\n", getnick1(arg1), arg1); */
                    break;
            }
        } else if (is3(com, "ADC")) {

        /* list add contact */

            char uuid[SNL];
            char lst = 0;
            msn_contact_t *q;
            int tid;

            uuid[0] = uuid[1] = uuid[2] = 0;
            arg1[1] = arg1[2] = arg2[1] = arg2[2] = 0;
            /* quick'n'dirty way instread of calling msn_parse_contact_data() */
            sscanf(s + 4, "%u %c%*s %s %s %s", &tid, &lst, arg1, arg2, uuid);
            if (arg2[0] == 'F' && arg2[1] == '=') url2str(&arg2[2], nick);

            switch (lst) {
                case 'F':
                    LOCK(&msn.lock);
                    if (arg1[0] == 'N') {
                        q = msn_clist_add(&msn.CL, msn_FL, &arg1[2], nick, NULL, &uuid[2]);
                        draw_lst(1);
                        msg(C_MSG, "%s <%s> has been added to your forward list\n",
                                getnick2(&arg1[2], nick), &arg1[2]);
                    } else if (arg1[0] == 'C') {
                        if (p = msn_clist_findu(&msn.CL, msn_FL, &arg1[2])) {
                            q = msn_clist_add(&msn.CL, msn_FL, p->login, p->nick, arg2, p->uuid);
                            draw_lst(1);
                            msg(C_MSG, "%s <%s> has been added to group %s\n",
                                    getnick1c(p), p->login, msn_glist_findn(&msn.GL, arg2));
                        }
                    }
                    UNLOCK(&msn.lock);
                    break;
                case 'A':
                    LOCK(&msn.lock);
                    msn_clist_add(&msn.CL, msn_AL, &arg1[2], NULL, NULL, NULL);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been added to your allow list\n", getnick1(&arg1[2]), &arg1[2]);
                    break;
                case 'B':
                    LOCK(&msn.lock);
                    msn_clist_add(&msn.CL, msn_BL, &arg1[2], NULL, NULL, NULL);
                    draw_lst(1);
                    UNLOCK(&msn.lock);
                    msg(C_MSG, "%s <%s> has been blocked\n", getnick1(&arg1[2]), &arg1[2]);
                    break;
                case 'R':
                    LOCK(&msn.lock);
                    q = msn_clist_add(&msn.CL, msn_RL, &arg1[2], NULL, NULL, NULL);
                    UNLOCK(&msn.lock);
                    if (tid == 0 && msn.GTC == 'A')
                        msg(C_MSG, "%s <%s> has added you to his/her contact list\n",
                                getnick1(&arg1[2]), &arg1[2]);
                    break;
                case 'P':
                    /* MSNP: possibly unreachable code */
                    LOCK(&msn.lock);
                    q = msn_clist_add(&msn.CL, msn_PL, &arg1[2], NULL, NULL, NULL);
                    UNLOCK(&msn.lock);
                    break;
            }
        } else if (is3(com, "LST")) {

        /* contact lists */

            unsigned int lflags;
            char uuid[SNL], *rest, *tmp;
            
            /*sscanf(s + 4, "%s %s %u %s", arg1, arg2, &lflags, gids);*/
            rest = msn_parse_contact_data(s+4, arg1, nick, uuid);
            /*msg(C_DBG, "PARSED login = %s, nick = %s, uuid = %s\nrest = %s\n", arg1, nick, gid, rest);*/
            sscanf(rest, "%d", &lflags);
			
            LOCK(&msn.lock);
            q = msn_clist_add(&msn.CL, lflags, arg1, nick, NULL, uuid);
            if (lflags & msn_FL) {
                tmp = strchr(rest, ' '); /* before list flags */
                if (tmp != NULL) tmp = strchr(tmp+1, ' '); /* before contact type */
                if (tmp != NULL) tmp = strchr(tmp+1, ' '); /* before groups */
                /* FL */
                if (tmp != NULL) {
                    int i;
                    tmp++;
                    for (i = 0; tmp[i]; i++) if (tmp[i] == ',') tmp[i] = ' ';
                    while (1) {
                        char gid[SNL];
                        if (sscanf(tmp, "%s", gid) == 1) {
                            /* msg(C_DBG, "parsed group %s for contact %s %s\n", gid, arg1, uuid); */
                            q = msn_clist_add(&msn.CL, msn_FL, arg1, nick, gid, uuid);
                        }
                        else break;
                        tmp = strchr(tmp, ' ');
                        if (tmp == NULL) break; else tmp++;
                    }
                }
            }
            /* if (q->groups) msg(C_DBG, "added contact: %s, groups = %d\n", q->login, q->groups); */
            if (lflags & (msn_PL | msn_RL)) {
                /* RL, PL */
                if ((q->lflags & (msn_FL | msn_AL | msn_BL)) == 0 && msn.GTC == 'A')
                    msg(C_MSG, "%s <%s> has added you to his/her contact list\n", getnick2(arg1, nick), arg1);
            }
            if (!(lflags & msn_RL) && (lflags & msn_FL) && msn.GTC == 'A') {
                msg(C_MSG, "%s <%s> may have removed you from his/her contact list\n", getnick2(arg1, nick), arg1);
            }
            if (--msn.list_count == 0) end_of_sync();
            UNLOCK(&msn.lock);
        } else if (is3(com, "SYN")) {

        /* sync begin */
            int nc = 0, ng = 0;
            sscanf(s + 4, "%*u %*s %*s %d %d", &nc, &ng);
            msn.list_count = nc + ng;
            msn.in_syn = 1;
/*            if (msn.SYN == syn_cache.ver) {
                LOCK(&msn.lock);
                read_syn_cache_data();
                msg(C_MSG, "Contacts loaded\n");
                draw_lst(1);
                * initial status to log in *
                msn_chg(msn.nfd, nftid(), Config.initial_status);
                UNLOCK(&msn.lock);
            } else {
                if (syn_cache_fp != NULL) fclose(syn_cache_fp);*/
                msg(C_MSG, "Retrieving contacts ...\n");
/*            }*/
            
        } else if (is3(com, "QRY")) {
            
            /* msg(C_MSG, "Challenge accepted\n"); */
            
        } else if (is3(com, "PRP")) {
			
            /* personal data (nick, phone numbers etc.) */

            if (msn.in_syn) sscanf(s + 4, "%s %s", arg1, arg2);
            else sscanf(s + 4, "%*u %s %s", arg1, arg2);

            if (is3(arg1, "MFN")) {

                /* my friendly name */
                /* screen name change */

                url2str(arg2, nick);
                LOCK(&msn.lock);
                Strcpy(msn.nick, nick, SML);
                draw_status(1);
                UNLOCK(&msn.lock);
            }

        } else if (is3(com, "SBS")) {

            /* mobile credits */
            /* ignore */

        } else if (is3(com, "BPR")) {
			
            /* user phone numbers, blogs, etc. */
            
            /* ignore */
            
        } else if (is3(com, "QNG")) {
            
        /* pong */
            if (show_ping_result > 0) {  /* ok, a harmless race condition! :-) */
                struct timeval tv;
    	        double sec;
                int isec, next = 0;
	            
                show_ping_result--;
                sscanf(s + 4, "%d", &next);
                gettimeofday(&tv, NULL);
                isec = tv.tv_sec - tv_ping.tv_sec;
                sec = ((double) isec + ((double) (tv.tv_usec - tv_ping.tv_usec)) / 1000000);
                msg(C_MSG, "RTT = %g sec, NEXT = %d\n", sec, next);
            }
        } else if (is3(com, "SBP")) {

        /* screen name change */

            char sett[SNL];

            sscanf(s + 4, "%*u %s %s %s", arg1, sett, arg2);

            if (is3(sett, "MFN")) {
                url2str(arg2, nick);
                LOCK(&msn.lock);
                if (msn_clist_update(&msn.CL, msn_FL, arg1, nick, -1, -1, 1) > 0)
                    draw_lst(1);
                UNLOCK(&msn.lock);
            }

        } else if (is3(com, "XFR")) {

        /* referral */
            char addr[SML];

            addr[0] = 0;
            sscanf(s + 4, "%*u %s %s %*s %s", arg1, addr, arg2);
            if (arg1[0] == 'N') {
                msg(C_ERR, "SERVER: You must connect connect to a new notification server\n"
                           "NEW ADDRESS: %s\n", addr);
                LOCK(&msn.lock);
                Strcpy(msn.notaddr, addr, SML);
                UNLOCK(&msn.lock);
                break;
            } else if (arg1[0] == 'S') {
                msn_sboard_t *sb;
                qelem_t *e = queue_get(&q_sb_req);
                
                if (e == NULL) {
                    msg(C_ERR, "missed new-sb request (this should not happen!)\n");
                } else {
                    int found;
                    
                    LOCK(&SX);

                    sb = SL.head;
                    found = 0;
                    if (e->type == 0 && sb != NULL) do {
                        if (sb == e->data) { /* look for the disconnected switchboard window */
                            found = 1;
                            break;
                        }
                        sb = sb->next;
                    } while (sb != SL.head);
                    if (!found) {
                        sb = msn_sblist_add(&SL, addr, arg2, ZS, ZS, 0, ZS);
                        SL.head = sb;
                        /* auto-invite */
                        if (e->type == 1 && e->data != NULL) strcpy(sb->master, e->data);
                    } else {
                        /* reuse disconnected switchboard */
                        Strcpy(sb->sbaddr, addr, SML);
                        Strcpy(sb->hash, arg2, SML);
                        sb->called = 0;
                        sb->sessid[0] = 0;
                        sboard_openlog(sb);
                    }
                    qelem_free(e);

                    draw_sb(sb, 1);
                    UNLOCK(&SX);
                    pthread_create(&sb->thrid, NULL, msn_sbdaemon, (void *) sb);
                }
            } else msgn(C_DBG, strlen(s)+6, ">>> %s\n", s);
        } else if (is3(com, "RNG")) {

        /* invitation to switchboard */
            char sbaddr[SML], hash[SML], sessid[SML], nf[SML];
            msn_sboard_t *sb;
            int found, ignore;

            sbaddr[0] = hash[0] = 0;
            sscanf(s + 4, "%s %s %*s %s %s %s", sessid, sbaddr, hash, arg1, arg2);
            url2str(arg2, nick);
            LOCK(&msn.lock);
            found = msn_clist_update(&msn.CL, msn_FL, arg1, nick, -1, -1, 0);
            if (found > 0) draw_lst(1);
            if (found == -1 && msn.BLP == 'B') {
                UNLOCK(&msn.lock);
            } else {
                msg(C_MSG, "%s <%s> rings\n", getnick2(arg1, nick), arg1);
                sprintf(nf, "%s rings\n", getnick2(arg1, nick));
                if (can_notif(arg1)) unotify(nf, SND_RING);
                p = msn_clist_find(&msn.CL, 0, arg1);
                if (p != NULL) ignore = p->ignored; else ignore = 0;
                if (!Config.invitable || ignore) {
                    UNLOCK(&msn.lock);
                    continue;
                }
                LOCK(&SX);
                sb = SL.head;
                found = 0;
                if (sb != NULL) do {
                    /* look for inactive switchboard that can be used again:
                            must have zero users
                            owner must be the same
                            must have not become multiuser */
                    if (sb->PL.count == 0 && strcmp(sb->master, arg1) == 0 && !sb->multi) {
                        found = 1;
                        break;
                    }
                    sb = sb->next;
                } while (sb != SL.head);
                if (!found) sb = msn_sblist_add(&SL, sbaddr, hash, arg1, nick, 1, sessid);
                else {
                    /* reuse inactive switchboard */
                    if (sb->thrid != (pthread_t) -1) {
                        pthread_t tmp = sb->thrid;
                        pthread_cancel(tmp);
                        pthread_join(tmp, NULL);
                    }
                    Strcpy(sb->sbaddr, sbaddr, SML);
                    Strcpy(sb->hash, hash, SML);
                    sb->called = 1;
                    Strcpy(sb->sessid, sessid, SML);
                    sboard_openlog(sb);
                }

                draw_sb(sb, 1);
                
                /* if only one window, activate it */
                if (SL.count == 1 && clmenu) {
                    clmenu = 0;
                    draw_lst(1);
                }
                
                UNLOCK(&SX);
                UNLOCK(&msn.lock);
                pthread_create(&sb->thrid, NULL, msn_sbdaemon, (void *) sb);
            }
        } else if (is3(com, "OUT")) {

        /* server disconnects user */

            sscanf(s + 4, "%s", arg1);
            if (is3(arg1, "OTH"))
                msg(C_ERR, "OUT: You are logging in from other location\n");
            else if (is3(arg1, "SSD")) msg(C_ERR, "OUT: Server is going down\n");
            else if (is3(arg1, "RCT")) msg(C_ERR, "OUT: You have been logged out due to inactivity\n");
            else msg(C_DBG, "%s", s);
            if (r == 5) msg(C_MSG, "Disconnected\n");
            break;
        } else if (is3(com, "NOT")) {
            
            /* sever notification -- just ignore */

            if (sscanf(s + 4, "%d", &msglen) == 1) {
                msgdata = msn_get_payload(&buf, msglen);
                if (Config.log_traffic && msglen > 0) {
                    fprintf(flog, "%s\n", msgdata);
                    fflush(flog);
                }
                free(msgdata);
            }

        } else if (is3(com, "LKP")) {

            /* lookup phone numbers */
            /* ignore */

        } else if (is3(com, "UUX")) {

            /* personal message */
            int stat;
            if (sscanf(s + 4, "%*d %d", &stat) == 1 && stat == 0) {
                msg(C_MSG, "Personal message set\n");
            }

        } else if (is3(com, "UBX")) {

            /* buddy personal message */
            if (sscanf(s + 4, "%s %d", arg1, &msglen) == 2) {
                char *psm1, *psm2;
                msgdata = msn_get_payload(&buf, msglen);
                if (Config.log_traffic && msglen > 0) {
                    fprintf(flog, "%s\n", msgdata);
                    fflush(flog);
                }
                if (Config.msg_debug == 2) {
                    msg(C_DBG, "Personal data for <%s>:\n", arg1);
                    msgn(C_DBG, strlen(msgdata)+2, "%s\n", msgdata);
                }
                psm1 = strafter(msgdata, "<PSM>");
                if (psm1) psm2 = strstr(psm1, "</PSM>");
                if (psm2) *psm2 = 0;
                if (psm1 && psm2) {
                    char psm[SML];
                    url2str(psm1, psm);
                    LOCK(&msn.lock);
                    if (msn_clist_psm_update(&msn.CL, arg1, psm) == 1)
                        msg(C_MSG, "%s <%s> - %s\n", getnick1(arg1), arg1, psm);
                    UNLOCK(&msn.lock);
                }
                free(msgdata);
            }

        } else if (is3(com, "GCF")) {

            /* global configuration file */
            if (sscanf(s + 4, "%*d %s %d", arg1, &msglen) == 2) {
                msgdata = msn_get_payload(&buf, msglen);
                if (Config.log_traffic && msglen > 0) {
                    fprintf(flog, "%s\n", msgdata);
                    fflush(flog);
                }
                if (Config.msg_debug >= 1) {
                    msg(C_DBG, "Received Global Configuration File '%s':\n", arg1);
                    msgn(C_DBG, strlen(msgdata)+2, "%s\n", msgdata);
                }
                free(msgdata);
            }

        } else

        /* error code or unhandled command */

            if (!scan_error(com)) msgn(C_DBG, strlen(s)+6, ">>> %s", s);
    }
    pthread_cleanup_pop(0);
    logout_cleanup((void *) r);
    pthread_detach(pthread_self());
    return NULL;
}

void msn_init(msn_t *msn)
{
    int i;
	
    Strcpy(msn->nick, msn->login, SML);
    msn->status = MS_FLN;
    msn->inbox = 0;
    msn->folders = 0;

    msn->BLP = msn->GTC = 0;

    msn->GL.head = NULL; msn->GL.count = 0;
    msn->CL.head = NULL; msn->CL.count = 0;
    msn->hlogin[0] = 0;
    msn->hgid[0] = 0;
    msn->dhid = 0;

    /* msn->IL.head = NULL; msn->IL.count = 0; */

    msn->list_count = -1;
    msn->flskip = 0;
    msn->nfd = -1;
    msn->in_syn = 0;
    msn->thrid = (pthread_t) -1;
    
    msn->fn_log[0] = 0;
    msn->fp_log = NULL;
	
}
