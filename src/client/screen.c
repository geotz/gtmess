/*
 *    screen.c
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

#include<pthread.h>
#include<string.h>
#include<stdlib.h>
#include<ctype.h>
#include<time.h>

#include"screen.h"
#include"sboard.h"
#include"unotif.h"
#include"xfer.h"
#include"utf8.h"
#include"queue.h"
#include"gtmess.h"

int C_NORMAL = A_NORMAL;
int C_ERR    = A_UNDERLINE;
int C_DBG    = A_BOLD;
int C_MSG    = A_NORMAL;
int C_MNU    = A_REVERSE;
int C_EBX    = A_NORMAL;
int C_GRP    = A_UNDERLINE;

int statattrs[] = {A_REVERSE, A_REVERSE, A_STANDOUT, A_REVERSE,
        A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE};
int lstatattrs[] = {A_NORMAL, A_NORMAL, A_BOLD, A_BOLD, A_BOLD,
        A_BOLD, A_BOLD, A_BOLD, A_BOLD, A_UNDERLINE};

TWindow w_msg, w_lst, w_back, w_xfer;
pthread_mutex_t scr_lock;
int scr_shutdown = 0;

int wvis; /* 0 = sb window visible, 1 = transfers window visible */
int clmenu; /* CL menu active */
int w_msg_top = 0;

/* last arguments to msg2() */
char msg2_str[SML] = {0};
int msg2_attr;

time_t msg_now = 0;

extern struct tm now_tm;


/* text message queue before screen init. */
xqueue_t msg_q; 

int fullscreen = 0;

float scr_rx = 0.75;
float scr_ry = 0.4;

int SCOLS, SLINES;
int utf8_mode;

char *get_next_word(char **text, char **word, int *len, int *numlines)
{
    char *s = *text;
    int nl = 0;
    /* skip whitespace */
    while ((*s) && isspace(*s)) {
        if (*s == '\n') nl++;
        s++;
    }
    if (!(*s)) {
        *text = s;
        *word = NULL;
        *len = 0;
        *numlines = nl;
        return NULL;
    }
    *word = s;
    while ((*s) && !(isspace(*s))) s++;
    *text = s;
    *len = (s - *word);
    *numlines = nl;
    return *word;
}

/* indent = initial indentation */
void wprintx(WINDOW *w, char *txt, int indent)
{
    int maxwidth, ymax, width, wlen, nlines;
    char *text, *word;
    char twbuf[SXL], *tw;
    int alloc;
    
    getmaxyx(w, ymax, maxwidth);
    maxwidth--;
    text = txt;
    width = indent;
    while (get_next_word(&text, &word, &wlen, &nlines) != NULL) {
        if (width > 0 && width+wlen >= maxwidth) waddch(w, '\n');
        tw = twbuf; alloc = 0;
        if (wlen >= SXL) {
            tw = (char *) malloc(wlen+1);
            alloc = 1;
        }
        strncpy(tw, word, wlen);
        tw[wlen] = 0;
        if (nlines > 0) for (; nlines > 0; --nlines) waddch(w, '\n');
        waddstr(w, tw);
        waddch(w, ' ');
        getyx(w, ymax, width);
        if (alloc && tw != NULL) free(tw);
    }
    for (; nlines > 0; --nlines) waddch(w, '\n');
}

void gs_draw_prompt(int attr, const char *prompt)
{
    move(SLINES - 1, 0);
    bkgdset(' ' | attr);
    clrtoeol();
    bkgdset(' ' | C_NORMAL);
    move(SLINES - 1, 0);
    attrset(attr ^ A_REVERSE);
    addstr(prompt);
    attrset(C_NORMAL);
}

void gs_draw_ebox(ebox_t *e, int attr, int pl)
{
    move(SLINES - 1, pl);
    attrset(attr);
    eb_draw(e, stdscr);
    attrset(C_NORMAL);
}

int get_string(int attr, int mask, const char *prompt, char *dest, size_t n)
{
    int r;
    int c, pl;
    ebox_t E;

    pl = strlen(prompt);
    eb_init(&E, SML-1, SCOLS-1-pl);
    E.utf8 = utf8_mode;
    E.mask = mask;
    eb_settext(&E, dest);
    c = -1;
    
    gs_draw_prompt(attr, prompt);    
    while (1) {
        if (c == 13 || c == 27) break;

        gs_draw_ebox(&E, attr, pl);
        c = getch();
        if (c == KEY_RESIZE) {
            redraw_screen(1);
            eb_resize(&E, SCOLS-1-pl); 
            gs_draw_prompt(attr, prompt);
            continue;
        }
        eb_keydown(&E, c);
    }
    r = (c == 13);
    if (r) {
        if (strcmp(dest, (char *) E.text) == 0) r <<= 1;
        else Strcpy(dest, (char *) E.text, n);
    }
    move(SLINES - 1, SCOLS - 1);
    eb_free(&E);
    return r;
}

/* print a message */
int vwmsg(TWindow *w, int size, time_t *sbtime, time_t *real_sbtime, int attr, 
        FILE *fp_log, const char *fmt, va_list ap)
{
    char tmpbuf[SXL], *tmp, *s;
    int alloc;
    time_t now2;
    int indent = 0;
    int trunc;

    if (size < SXL) {
        tmp = tmpbuf;
        alloc = 0;
    } else {
        tmp = (char *) malloc(size);
        alloc = 1;
    }

    trunc = vsnprintf(tmp, size, fmt, ap);
    if (trunc < size) trunc = 0; else tmp[size-1] = 0;

    for (s = tmp; *s; s++) if (*s == '\r') *s = ' ';

    LOCK(&w->lock);
    wattrset(w->wh, attr);
    if (fp_log != NULL) {
        if (attr == C_DBG)
            fprintf(fp_log, "(--) "); 
        else if (attr == C_ERR)
            fprintf(fp_log, "(!!) ");
        else fprintf(fp_log, "     ");
        fflush(fp_log);
    }
/*    waddstr(w->wh, tmp);*/
    time(&now2);
    /* BUG: ugly hack to ignore "disconnected..." msgs */
    if (attr != C_ERR) *real_sbtime = now2;
    LOCK(&time_lock);
    if (difftime(now2, *sbtime) > 60) {
        *sbtime = now2;
        *sbtime -= now2 % 60;
        wprintw(w->wh, "[%02d:%02d] ", now_tm.tm_hour, now_tm.tm_min);
        if (fp_log != NULL) {
            fprintf(fp_log, "[%02d:%02d] ", now_tm.tm_hour, now_tm.tm_min);
            fflush(fp_log);
        }
        indent = 8;
    } else indent = 0;
    UNLOCK(&time_lock);
    wprintx(w->wh, tmp, indent);
    wattrset(w->wh, C_NORMAL);
    UNLOCK(&w->lock);
    if (fp_log != NULL) {
        fputs(tmp, fp_log);
        fflush(fp_log);
    }
    
    if (alloc && tmp != NULL) free(tmp);
    return trunc;
}

/* message window */
void msg(int attr, const char *fmt, ...)
{
    va_list ap;
    int r;
    
    va_start(ap, fmt);
    r = vmsg(attr, SML, fmt, ap);
    va_end(ap);
    if (r) vmsg(C_ERR, SML, "msg(): output truncated\n", NULL);
}

void msgn(int attr, int size, const char *fmt, ...)
{
    va_list ap;
    int r;
    
    va_start(ap, fmt);
    r = vmsg(attr, size, fmt, ap);
    va_end(ap);
    if (r) vmsg(C_ERR, SML, "msgn(): output truncated\n", NULL);
}

int screen_shut()
{
    int v = 0;
    LOCK(&scr_lock);
    v = scr_shutdown;
    UNLOCK(&scr_lock);
    return v;
}

int vmsg(int attr, int size, const char *fmt, va_list ap)
{
    char txtbuf[SML], *txt;
    int alloc;
    char *s;
    int y, x, newtop;
    time_t now2;
    int indent = 0;
    int trunc = 0;
    
    if (screen_shut()) return 0;
    
    if (size < SML) {
        txt = txtbuf;
        alloc = 0;
    } else {
        txt = (char *) malloc(size);
        alloc = 1;
    }
    trunc = vsnprintf(txt, size, fmt, ap);
    if (trunc < size) trunc = 0; else txt[size-1] = 0;

    for (s = txt; *s; s++) if (*s == '\r') *s = ' ';
    
    if (!fullscreen) {
        queue_put(&msg_q, 0, txt, strlen(txt) + 1);
        fprintf(stderr, "%s\n", txt);
        if (alloc && txt != NULL) free(txt);
        return 0;
    }
    
    LOCK(&w_msg.lock);
    wattrset(w_msg.wh, attr);
    if (msn.fp_log != NULL) {
        if (attr == C_DBG)
            fprintf(msn.fp_log, "(--) ");
        else if (attr == C_ERR)
            fprintf(msn.fp_log, "(!!) ");
        else fprintf(msn.fp_log, "     ");
        fflush(msn.fp_log);
    }
    time(&now2);
    LOCK(&time_lock);
    if (difftime(now2, msg_now) > 60) {
        msg_now = now2;
        msg_now -= now2 % 60;
        wprintw(w_msg.wh, "[%02d:%02d] ", now_tm.tm_hour, now_tm.tm_min);
        if (msn.fp_log != NULL) {
            fprintf(msn.fp_log, "[%02d:%02d] ", now_tm.tm_hour, now_tm.tm_min);
            fflush(msn.fp_log);
        }
        indent = 8;
    } else indent = 0;
    UNLOCK(&time_lock);
    wprintx(w_msg.wh, txt, indent);
    if (msn.fp_log != NULL) {
        fputs(txt, msn.fp_log);
        fflush(msn.fp_log);
    }
    wattrset(w_msg.wh, C_NORMAL);
    getyx(w_msg.wh, y, x);
    newtop = y - w_msg.h + 1;
    if (w_msg_top < newtop) w_msg_top = newtop;
    if (w_msg_top < 0) w_msg_top = 0;
    LOCK(&scr_lock);
    getyx(stdscr, y, x);
    copywin(w_msg.wh, stdscr, w_msg_top, 0, w_msg.y, w_msg.x,
            w_msg.y + w_msg.h - 1, w_msg.x + w_msg.w - 1, FALSE);
    UNLOCK(&w_msg.lock);
    move(y, x);
    refresh();
    UNLOCK(&scr_lock);
    if (alloc && txt != NULL) free(txt);
    return trunc;
}

void msg2(int attr, const char *fmt, ...)
{
    va_list ap;
    char txt[SML];
    int y, x;

    if (screen_shut()) return;

    va_start(ap, fmt);
    vsprintf(txt, fmt, ap);
    va_end(ap);
    Strcpy(msg2_str, txt, SML);
    msg2_attr = attr;
    if (utf8_mode) txt[widthoffset(txt, SCOLS-2)] = 0;
    else txt[SCOLS-2] = 0;
    LOCK(&scr_lock);
    getyx(stdscr, y, x);
    move(SLINES - 1, 0);
    bkgdset(' ' | attr);
    clrtoeol();
    bkgdset(' ' | C_NORMAL);
    attrset(attr);
    addstr(txt);
    attrset(C_NORMAL);
    move(y, x);
    refresh();
    UNLOCK(&scr_lock);
}

void draw_rest()
{
    int y, x;

    getyx(stdscr, y, x);
    move(1, (int) (SCOLS * scr_rx + 0.5));
    vline(ACS_VLINE, SLINES - 2);
    mvaddch(w_msg.y - 1, (int) (SCOLS * scr_rx + 0.5), ACS_RTEE);
    move(y, x);
}

void draw_status(int r)
{
    int y, x;
    char txt[SML];
    
    sprintf(txt, "%s (%s)", msn.nick, msn_stat_name[msn.status]);
    if (utf8_mode) txt[widthoffset(txt, SCOLS-6)] = 0;
    else txt[SCOLS-6] = 0;
    getyx(stdscr, y, x);
    move(0, 0);
    attrset(statattrs[msn.status]);
    printw("%*c", SCOLS-5, ' ');
/*    bkgdset(' ' | statattrs[msn.status]);
    clrtoeol();
    bkgdset(' ' | C_NORMAL);*/
    move(0, 0);
    addstr(txt); 
/*    addch(' ');
    addch(ACS_VLINE); 
    printw(" gtmess %s", VERSION);*/
    attrset(C_NORMAL);
    move(y, x);
    draw_time(r);
}

void draw_time(int r)
{
    int y, x;
    
    getyx(stdscr, y, x);
    move(0, SCOLS-5);
    bkgdset(' ' | statattrs[msn.status]);
    clrtoeol();
    bkgdset(' ' | C_NORMAL);
    attrset(statattrs[msn.status]);
    LOCK(&time_lock);
    printw("%02d:%02d", now_tm.tm_hour, now_tm.tm_min);
    UNLOCK(&time_lock);
    attrset(C_NORMAL);
    move(y, x);
    if (r) refresh();
}


/* VERY tricky drawing routine, since data are not static :-/ */
void draw_lst(int r)
{
    msn_contact_t *p;
    msn_group_t *g;
    time_t now;
    char ch;
    int lines, total;
    int y, x, i, j, ingroup;
    int cur_grp, cur_con;
    int gcount = msn.GL.count + 1;
    msn_group_t *b[gcount];
    msn_contact_t **a[gcount];
    int gsize[gcount];
    int found;
    
    total = gcount;
    cur_grp = 0;
    cur_con = 0;
    found = 0;
    if (Config.online_only == 0) {
        for (p = msn.CL.head; p != NULL; p = p->next)
            if (p->lflags & msn_FL) {
                if (p->groups > 0) total += p->groups;
                else total++;
            }
    } else {
        for (p = msn.CL.head; p != NULL; p = p->next)
            if ((p->lflags & msn_FL) && p->status >= MS_NLN) {
                if (p->groups > 0) total += p->groups;
                else total++;
            }
    }
    
    /* create arrays for random access */
    for (i = 0, g = msn.GL.head; g != NULL; g = g->next, i++) {
        b[i] = g;
        gsize[i] = 0;
        for (p = msn.CL.head; p != NULL; p = p->next) { /* first pass to compute size :-/ */
            if ((p->lflags & msn_FL) == 0 || (Config.online_only && p->status < MS_NLN)) continue;
            for (j = 0; j < p->groups; j++) {
                gsize[i] += strcmp(p->gid[j], g->gid) == 0;
            }
        }
        if (gsize[i]) {
            /* is there an easy way to avoid malloc() all the time? perhaps a custom malloc :-) */
            a[i] = (msn_contact_t **) malloc(gsize[i] * sizeof(msn_contact_t *));
            ingroup = 0;
            for (p = msn.CL.head; p != NULL; p = p->next) {
                if ((p->lflags & msn_FL) == 0 || (Config.online_only && p->status < MS_NLN)) continue;
                for (j = 0; j < p->groups; j++) if (strcmp(p->gid[j], g->gid) == 0) {
                    a[i][ingroup] = p;
                    if (strcmp(msn.hgid, g->gid) == 0 && strcmp(p->login, msn.hlogin) == 0)
                        cur_grp = i, cur_con = ingroup, found = 1;
                    ingroup++;
                }
            }
        }
    }
    /* groupless contacts */
    b[gcount-1] = &msn_group_0;
    gsize[gcount-1] = 0;
    for (p = msn.CL.head; p != NULL; p = p->next)
        if ((p->lflags & msn_FL) && (!Config.online_only || p->status >= MS_NLN) && p->groups == 0)
            gsize[gcount-1]++;
    if (gsize[gcount-1]) {
        a[gcount-1] = (msn_contact_t **) malloc(gsize[gcount-1] * sizeof(msn_contact_t *));
        ingroup = 0;
        for (p = msn.CL.head; p != NULL; p = p->next)
            if ((p->lflags & msn_FL)
                && (!Config.online_only || p->status >= MS_NLN) && p->groups == 0) {
                a[gcount-1][ingroup] = p;
                if (msn.hgid[0] == 0 && strcmp(p->login, msn.hlogin) == 0)
                    cur_grp = gcount-1, cur_con = ingroup, found = 1;
                ingroup++;
            }
    }

    lines = 0;
    
    /* handle selection */
    if (found) {
        if (msn.dhid > 0) {
            cur_con++;
            while (1) {
                if (cur_con >= gsize[cur_grp]) {
                    cur_grp++;
                    if (cur_grp >= gcount) cur_grp = 0;
                    cur_con = 0; 
                } else break;
            }
            msn.dhid = 0;
        } else if (msn.dhid < 0) {
            cur_con--;
            while (1) {
                if (cur_con < 0) {
                    cur_grp--; 
                    if (cur_grp < 0) cur_grp = gcount - 1;
                    cur_con = gsize[cur_grp] - 1;
                } else break;
            }
            msn.dhid = 0;
        }
    } else while (cur_grp < gcount) {
        if (cur_con < gsize[cur_grp]) break;
        else cur_grp++, cur_con = 0;
    }

    if (cur_grp >= gcount) {
        clmenu = 0;
    }
    
    if (clmenu) {
        if (b[cur_grp]->gid[0]) strcpy(msn.hgid, b[cur_grp]->gid);
        else msn.hgid[0] = 0;
        strcpy(msn.hlogin, a[cur_grp][cur_con]->login);
    }

    /* do the drawing */
    LOCK(&w_lst.lock);
    wclear(w_lst.wh);
    
    time(&now);
    for (i = 0; i < gcount; i++) {
        if (lines >= msn.flskip && lines < total + msn.flskip) {
            wattrset(w_lst.wh, C_GRP);
            wprintw(w_lst.wh, "%s\n", b[i]->name);
        }
        lines++;
        for (j = 0; j < gsize[i]; j++) {
            if (lines >= msn.flskip && lines < total + msn.flskip) {
                p = a[i][j];
                if (clmenu && i == cur_grp && j == cur_con)
                    wattrset(w_lst.wh, statattrs[p->status]);
                else wattrset(w_lst.wh, lstatattrs[p->status]);
                if (now - p->tm_last_char <= Config.time_user_types) ch = '!';
                else if ((p->lflags & msn_BL) == msn_BL) ch = '+'; /* blocked */
                else if (p->ignored) ch = ':';
                else ch = ' ';
                wprintw(w_lst.wh, "%c%c %s\n", ch, msn_stat_char[p->status], getnick1c(p));
            }
            lines++;
        }
    }

    for (i = 0; i < gcount; i++) if (gsize[i]) free(a[i]);

    getyx(stdscr, y, x);
    copywin(w_lst.wh, stdscr, 0, 0, w_lst.y, w_lst.x,
            w_lst.y + w_lst.h - 1, w_lst.x + w_lst.w - 1, FALSE);
    UNLOCK(&w_lst.lock);
    move(y, x);
    
    if (r) refresh();
}

void draw_msg(int r)
{
    int y, x;
    
    getyx(stdscr, y, x);
    LOCK(&w_msg.lock);
    copywin(w_msg.wh, stdscr, w_msg_top, 0, w_msg.y, w_msg.x,
            w_msg.y + w_msg.h - 1, w_msg.x + w_msg.w - 1, FALSE);
    UNLOCK(&w_msg.lock);
    move(y, x);
    if (r) refresh();
}

void draw_dlg(msn_sboard_t *sb, int r)
{
    TWindow *w;
    int x, y, ty;
    
    if (sb == NULL) w = &w_back;
    else {
        w = &sb->w_dlg;
        /*if (sb->pending == 1) unotify(ZS, SND_PENDING), sb->pending++;*/
/*        else if (sb != SL.head && sb->pending) unotify(ZS, SND_PENDING);*/
    }
    if (wvis != 0) return;
    if (sb == SL.head) {
        getyx(stdscr, y, x);
        LOCK(&w->lock);
        if (sb != NULL) ty = sb->w_dlg_top; else ty = 0;
        
        copywin(w->wh, stdscr, ty, 0, W_DLG_Y, W_DLG_X,
                W_DLG_Y + W_DLG_H - 1, W_DLG_X + W_DLG_W - 1, FALSE);
        UNLOCK(&w->lock);
        move(y, x);
        if (r) refresh();
    }
}

/* switchboard participants */
void draw_plst(msn_sboard_t *sb, int r)
{
    msn_contact_t *p;
    TWindow *w;
    int lines, total;
    int y, x;

    if (wvis != 0) return;
    if (sb == NULL) w = &w_back; else w = &sb->w_prt;
    LOCK(&w->lock);
    wclear(w->wh);

    if (sb != NULL) {
        lines = 0;
        total = sb->PL.count + 1;
        if (lines >= sb->plskip && lines < total + sb->plskip) {
            wattrset(w->wh, C_GRP);
            wprintw(w->wh, "[%s]\n\n", getnick2(sb->master, sb->mnick));
        }
        lines++;
        for (p = sb->PL.head; p != NULL; p = p->next) {
            if (lines >= sb->plskip && lines < total + sb->plskip) {
                wattrset(w->wh, lstatattrs[MS_UNK]);
                wprintw(w->wh, "  %s\n", getnick1c(p));
            }
            lines++;
        }
    }
    UNLOCK(&w->lock);
    if (sb == SL.head) {
        getyx(stdscr, y, x);
        LOCK(&w->lock);
        copywin(w->wh, stdscr, 0, 0, W_PRT_Y, W_PRT_X,
                W_PRT_Y + W_PRT_H - 1, W_PRT_X + W_PRT_W - 1, FALSE);
        UNLOCK(&w->lock);
        move(y, x);
        if (r) refresh();
    }
}

void draw_sbbar(int r)
{
    int i, c;
    int left, leftp, right, rightp;
    msn_sboard_t *s;
    int attr;
    char txt[SML];
    char *ch;
    int y, x, w;
    
    ch = &txt[0];
    attr = C_NORMAL;
    
    s = SL.start;
    c = SL.count;
    if (c > 0) {
        w = w_msg.w / SL.count;
        if (w < 9) w = 9;
    } else w = 1;
    if (c < w_msg.w) {
        txt[0] = 0;
        for (i = 0; i < c; i++, s = s->next) {
            if (s == SL.head) strcat(txt, "[");
            else if (s->pending) strcat(txt, "{");
            else strcat(txt, " ");
            strncat(txt, s->mnick, w-3);
            if (s == SL.head) strcat(txt, "]-");
            else if (s->pending) strcat(txt, "}-");
            else strcat(txt, " -");
        }
        if (strwidth(txt) > w_msg.w) {
            for (i = 0; i < c; i++, s = s->next) {
               if (s == SL.head) *ch++ = 'O';
               else if (s->pending) *ch++ = '+';
               else *ch++ = '-';
            }
            *ch = 0;
        }
    } else {
        left = leftp = right = rightp = 0;
        for (left = 0; s != SL.head; s = s->next, left++) if (s->pending) leftp++;
        s = s->next;
        for (right = 0; s != SL.start; s = s->next, right++) if (s->pending) right++;
        sprintf(ch, "(+%d/%d)[%d](+%d/%d)", leftp, left, c, rightp, right);
    }

    getyx(stdscr, y, x);
    move(w_msg.y - 1, 0);
    for (i = 0; i < w_msg.w; i++) addch(ACS_HLINE);
    move(w_msg.y - 1, 0);
    attrset(attr);
    addstr(txt);
    move(y, x);
    
    if (r) refresh();
}

void dlg(int attr, const char *fmt, ...)
{
    va_list ap;
    int r;
    
    va_start(ap, fmt);
    r = vdlg(attr, SXL, fmt, ap);
    va_end(ap);
    if (r) msg(C_ERR, "dlgn(): output truncated\n");
}

void dlgn(int attr, int size, const char *fmt, ...)
{
    va_list ap;
    int r;
    
    va_start(ap, fmt);
    r = vdlg(attr, size, fmt, ap);
    va_end(ap);
    if (r) msg(C_ERR, "dlgn(): output truncated\n");
}


/* switchboard conversation window */
int vdlg(int attr, int size, const char *fmt, va_list ap)
{
    msn_sboard_t *sb = (msn_sboard_t *) pthread_getspecific(key_sboard);
    int y, x, newtop;
    int r;

    if (sb == NULL) return 0;
    r = vwmsg(&sb->w_dlg, size, &sb->dlg_now, &sb->real_dlg_now, attr, sb->fp_log, fmt, ap);
    
    getyx(sb->w_dlg.wh, y, x);
    newtop = y - sb->w_dlg.h + 1;
    if (sb->w_dlg_top < newtop) sb->w_dlg_top = newtop;
    if (sb->w_dlg_top < 0) sb->w_dlg_top = 0;

    draw_dlg(sb, 0);
    draw_sbbar(1);
    return r;
}

void draw_sbebox(msn_sboard_t *sb, int r)
{
    if (wvis != 0) return;
    if (sb != SL.head) return;
    
    move(w_msg.y - 2, 0);
    if (sb != NULL) {
        attrset(C_EBX);
        eb_draw(&sb->EB, stdscr);
    } else {
        attrset(C_NORMAL);
        printw("%*c", w_msg.w, ' ');
        move(SLINES - 1, SCOLS - 1);
    }
    /* if (sb == NULL || clmenu == 1)  ... */
    attrset(C_NORMAL);
    
    if (r) refresh();
}

void draw_sb(msn_sboard_t *sb, int r)
{
    if (wvis == 0) {
        draw_plst(sb, 0);
        draw_dlg(sb, 0);
        draw_sbebox(sb, 0);
    } else draw_xfer(0);
    draw_sbbar(r);
}

void draw_all()
{
    draw_msg(0);
    draw_status(0);
    draw_lst(0);
    draw_sb(SL.head, 0);
    draw_rest(1);
}

void color_test()
{
    int i;
    msg(C_NORMAL, "Normal text\n");
    msg(C_ERR, "Error message\n");
    msg(C_DBG, "Debugging message\n");
    msg(C_MSG, "Server message\n");
    msg(C_MNU, "Menu entry\n");
    msg(C_EBX, "Editbox\n");
    msg(C_GRP, "Group name\n");
    
    msg(C_NORMAL, "\nContact colors:\n");
    for (i = MS_FLN; i <= MS_UNK; i++)
        msg(lstatattrs[i], "%s\n", msn_stat_name[i]);
    msg(C_NORMAL, "\nStatus colors:\n");
    for (i = MS_FLN; i <= MS_UNK; i++)
        msg(statattrs[i], "%s\n", msn_stat_name[i]);
}

void screen_resize()
{
#ifdef HAVE_WRESIZE
    msn_sboard_t *sb;
    
    if (COLS < 9) SCOLS = 9; else SCOLS = COLS;
    if (SLINES < 9) SLINES = 9; else SLINES = LINES;
    
    LOCK(&w_msg.lock);
    w_msg.w = (int) (SCOLS * scr_rx + 0.5);
    w_msg.h = (int) (SLINES * scr_ry + 0.5) - 2;
    wresize(w_msg.wh, W_MSG_LBUF, w_msg.w);
    w_msg.x = 0;
    w_msg.y = SLINES - 1 - w_msg.h;
    UNLOCK(&w_msg.lock);

    LOCK(&w_xfer.lock);
    w_xfer.h = W_DLG_H + 1;
    w_xfer.w = W_DLG_W + W_PRT_W;
    w_xfer.x = W_DLG_X;
    w_xfer.y = W_DLG_Y;
    wresize(w_xfer.wh, w_xfer.h, w_xfer.w);
    UNLOCK(&w_xfer.lock);
    
    LOCK(&w_lst.lock);
    w_lst.w = SCOLS - w_msg.w - 1;
    w_lst.h = SLINES - 2;
    wresize(w_lst.wh, w_lst.h, w_lst.w);
    w_lst.x = w_msg.x + w_msg.w + 1;
    w_lst.y = 1;
    UNLOCK(&w_lst.lock);

    LOCK(&w_back.lock);
    w_back.w = SCOLS;
    w_back.h = SLINES;
    wresize(w_back.wh, w_back.h, w_back.w);
    w_back.x = 0;
    w_back.y = 0;
    UNLOCK(&w_back.lock);
    
    sb = SL.head;
    if (sb != NULL) do {
        LOCK(&sb->w_dlg.lock);
        sb->w_dlg.w = W_DLG_W;
        sb->w_dlg.h = W_DLG_H;
        wresize(sb->w_dlg.wh, W_DLG_LBUF, sb->w_dlg.w);
        sb->w_dlg.x = W_DLG_X;
        sb->w_dlg.y = W_DLG_Y;
        UNLOCK(&sb->w_dlg.lock);
        LOCK(&sb->w_prt.lock);
        sb->w_prt.w = W_PRT_W;
        sb->w_prt.h = W_PRT_H;
        wresize(sb->w_prt.wh, sb->w_prt.h, sb->w_prt.w);
        sb->w_prt.x = W_PRT_X;
        sb->w_prt.y = W_PRT_Y;
        UNLOCK(&sb->w_prt.lock);
        eb_resize(&sb->EB, w_msg.w);
        sb = sb->next;
    } while (sb != SL.head);
#endif
}

void screen_init(int colors)
{
    qelem_t *q;

    initscr();
    if (colors == 1 || (colors == 0 && has_colors())) {
        start_color();
#ifdef HAVE_USE_DEFAULT_COLORS
        use_default_colors();
#define COLOR_BG -1
#else
#define COLOR_BG COLOR_BLACK
#endif
        init_pair(1, COLOR_RED, COLOR_BG);
        lstatattrs[MS_FLN] = lstatattrs[MS_HDN] =
                C_ERR = COLOR_PAIR(1);

        init_pair(2, COLOR_GREEN, COLOR_BG);
        lstatattrs[MS_NLN] = C_DBG = COLOR_PAIR(2);

        init_pair(3, COLOR_CYAN, COLOR_BG);
        lstatattrs[MS_BRB] = C_MSG = COLOR_PAIR(3);

        init_pair(4, COLOR_WHITE, COLOR_RED);
        statattrs[MS_FLN] = COLOR_PAIR(4);

        init_pair(5, COLOR_BLUE, COLOR_CYAN);
        statattrs[MS_NLN] = C_MNU = COLOR_PAIR(5);
        statattrs[MS_HDN] = C_EBX = COLOR_PAIR(5) | A_REVERSE;

        init_pair(6, COLOR_MAGENTA, COLOR_BG);
        lstatattrs[MS_BSY] = lstatattrs[MS_PHN] = COLOR_PAIR(6);

        init_pair(7, COLOR_BLACK, COLOR_CYAN);
        statattrs[MS_IDL] = statattrs[MS_AWY] =
                statattrs[MS_LUN] = COLOR_PAIR(7);

        init_pair(8, COLOR_WHITE, COLOR_MAGENTA);
        statattrs[MS_BSY] = statattrs[MS_PHN] = COLOR_PAIR(8);

        init_pair(9, COLOR_BLACK, COLOR_GREEN);
        statattrs[MS_BRB] = COLOR_PAIR(9);

        init_pair(10, COLOR_YELLOW, COLOR_BG);
        lstatattrs[MS_IDL] = lstatattrs[MS_AWY] =
                lstatattrs[MS_LUN] = COLOR_PAIR(10) | A_BOLD;

        init_pair(11, COLOR_BLUE, COLOR_BG);
        C_GRP = COLOR_PAIR(11);
        
        init_pair(12, COLOR_WHITE, COLOR_BG);
        C_NORMAL = lstatattrs[MS_UNK] = COLOR_PAIR(12);
    }
    msg2_attr = C_MNU;
    
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    scrollok(stdscr, TRUE);
    attrset(C_NORMAL);

    clear();
    
    SLINES = LINES;
    SCOLS = COLS;
    
    move(SLINES - 1, SCOLS - 1);
    refresh();

    w_msg.w = (int) (SCOLS * scr_rx + 0.5);
    w_msg.h = (int) (SLINES * scr_ry + 0.5) - 2;
    w_msg.wh = newpad(W_MSG_LBUF, w_msg.w);
/*    w_msg.wh = newwin(w_msg.h, w_msg.w, 0, 0);*/
    scrollok(w_msg.wh, TRUE);
    w_msg.x = 0;
    w_msg.y = SLINES - 1 - w_msg.h;
    pthread_mutex_init(&w_msg.lock, NULL);
    
    w_xfer.h = W_DLG_H + 1;
    w_xfer.w = W_DLG_W + W_PRT_W;
    w_xfer.x = W_DLG_X;
    w_xfer.y = W_DLG_Y;
    w_xfer.wh = newwin(w_xfer.h, w_xfer.w, 0, 0);
    pthread_mutex_init(&w_xfer.lock, NULL);

    w_lst.w = SCOLS - w_msg.w - 1;
    w_lst.h = SLINES - 2;
    w_lst.wh = newwin(w_lst.h, w_lst.w, 0, 0);
    scrollok(w_lst.wh, TRUE);
    w_lst.x = w_msg.x + w_msg.w + 1;
    w_lst.y = 1;
    pthread_mutex_init(&w_lst.lock, NULL);

    w_back.w = SCOLS;
    w_back.h = SLINES;
    w_back.wh = newwin(w_back.h, w_back.w, 0, 0);
    w_back.x = 0;
    w_back.y = 0;
    
    wvis = 0;
    clmenu = 0;

    pthread_mutex_init(&w_back.lock, NULL);

    pthread_mutex_init(&scr_lock, NULL);
    
    fullscreen = 1;
    while ((q = queue_get(&msg_q)) != NULL) {
        msg(C_ERR, "%s", q->data);
        qelem_free(q);
    }
}

void screen_done()
{
    delwin(w_msg.wh);
    erase(); refresh();
    endwin();
}
