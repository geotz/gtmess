/*
 *    screen.c
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

int attrs[] = {A_NORMAL, A_UNDERLINE, A_BOLD, A_NORMAL, A_REVERSE, A_NORMAL, A_NORMAL};
int statattrs[] = {A_REVERSE, A_REVERSE, A_STANDOUT, A_REVERSE,
        A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE};
int lstatattrs[] = {A_NORMAL, A_NORMAL, A_REVERSE, A_BOLD, A_BOLD,
        A_BOLD | A_UNDERLINE, A_BOLD, A_BOLD | A_UNDERLINE, A_BOLD, A_STANDOUT};

TWindow w_msg, w_lst, w_back, w_xfer;
pthread_mutex_t scr_lock;
int scr_shutdown = 0;

int wvis; /* 0 = sb window visible, 1 = transfers window visible */
int w_msg_top = 0;

/* last arguments to msg2() */
char msg2_str[SML] = {0};
cattr_t msg2_attr = C_MNU;

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

void gs_draw_prompt(cattr_t attr, const char *prompt)
{
    move(SLINES - 1, 0);
    bkgdset(' ' | attrs[attr]);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);
    move(SLINES - 1, 0);
    attrset(attrs[attr] ^ A_REVERSE);
    addstr(prompt);
    attrset(attrs[C_NORMAL]);
}

void gs_draw_ebox(ebox_t *e, cattr_t attr, int pl)
{
    move(SLINES - 1, pl);
    attrset(attrs[attr]);
    eb_draw(e, stdscr);
    attrset(attrs[C_NORMAL]);
}

int get_string(cattr_t attr, int mask, const char *prompt, char *dest, size_t n)
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
void vwmsg(TWindow *w, int size, time_t *sbtime, time_t *real_sbtime, cattr_t attr, 
        FILE *fp_log, const char *fmt, va_list ap)
{
    char tmpbuf[SXL], *tmp, *s;
    int alloc;
    time_t now2;
    int indent = 0;

    if (size < SXL) {
        tmp = tmpbuf;
        alloc = 0;
    } else {
        tmp = (char *) malloc(size);
        alloc = 1;
    }

    vsprintf(tmp, fmt, ap);

    for (s = tmp; *s; s++) if (*s == '\r') *s = ' ';

    LOCK(&w->lock);
    wattrset(w->wh, attrs[attr]);
    if (fp_log != NULL) {
        switch (attr) {
            case C_DBG: 
                fprintf(fp_log, "(--) "); break;
            case C_ERR: 
                fprintf(fp_log, "(!!) "); break;
            default:
                fprintf(fp_log, "     "); break;
        }
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
    wattrset(w->wh, attrs[C_NORMAL]);
    UNLOCK(&w->lock);
    if (fp_log != NULL) {
        fputs(tmp, fp_log);
        fflush(fp_log);
    }
    
    if (alloc && tmp != NULL) free(tmp);
}

/* message window */
void msg(cattr_t attr, const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vmsg(attr, SML, fmt, ap);
    va_end(ap);
}

void msgn(cattr_t attr, int size, const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vmsg(attr, size, fmt, ap);
    va_end(ap);
}

int screen_shut()
{
    int v = 0;
    LOCK(&scr_lock);
    v = scr_shutdown;
    UNLOCK(&scr_lock);
    return v;
}

void vmsg(cattr_t attr, int size, const char *fmt, va_list ap)
{
    char txtbuf[SML], *txt;
    int alloc;
    char *s;
    int y, x, newtop;
    time_t now2;
    int indent = 0;
    
    if (screen_shut()) return;
    
    if (size < SML) {
        txt = txtbuf;
        alloc = 0;
    } else {
        txt = (char *) malloc(size);
        alloc = 1;
    }
    vsprintf(txt, fmt, ap);
    for (s = txt; *s; s++) if (*s == '\r') *s = ' ';
    
    if (!fullscreen) {
        queue_put(&msg_q, 0, strdup(txt), strlen(txt) + 1);
        fprintf(stderr, "%s\n", txt);
        if (alloc && txt != NULL) free(txt);
        return;
    }
    
    LOCK(&w_msg.lock);
    wattrset(w_msg.wh, attrs[attr]);
    if (msn.fp_log != NULL) {
        switch (attr) {
            case C_DBG: 
                fprintf(msn.fp_log, "(--) "); break;
            case C_ERR: 
                fprintf(msn.fp_log, "(!!) "); break;
            default:
                fprintf(msn.fp_log, "     "); break;
        }
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
    wattrset(w_msg.wh, attrs[C_NORMAL]);
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
}

void msg2(cattr_t attr, const char *fmt, ...)
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
    bkgdset(' ' | attrs[attr]);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);
    attrset(attrs[attr]);
    addstr(txt);
    attrset(attrs[C_NORMAL]);
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
    
    sprintf(txt, "%s (%s)", getnick(msn.login, msn.nick, Config.aliases), msn_stat_name[msn.status]);
    if (utf8_mode) txt[widthoffset(txt, SCOLS-6)] = 0;
    else txt[SCOLS-6] = 0;
    getyx(stdscr, y, x);
    move(0, 0);
    attrset(statattrs[msn.status]);
    printw("%*c", SCOLS-5, ' ');
/*    bkgdset(' ' | statattrs[msn.status]);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);*/
    move(0, 0);
    addstr(txt); 
/*    addch(' ');
    addch(ACS_VLINE); 
    printw(" gtmess %s", VERSION);*/
    attrset(attrs[C_NORMAL]);
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
    bkgdset(' ' | attrs[C_NORMAL]);
    attrset(statattrs[msn.status]);
    LOCK(&time_lock);
    printw("%02d:%02d", now_tm.tm_hour, now_tm.tm_min);
    UNLOCK(&time_lock);
    attrset(attrs[C_NORMAL]);
    move(y, x);
    if (r) refresh();
}


void draw_lst(int r)
{
    msn_contact_t *p;
    msn_group_t *g;
    time_t now;
    char ch;
    int lines, total;
    int y, x;

    LOCK(&w_lst.lock);
    wclear(w_lst.wh);

    lines = 0;
    total = msn.GL.count;
    if (Config.online_only == 0) total += msn.FL.count;
    else 
        for (p = msn.FL.head; p != NULL; p = p->next)
            if (p->status >= MS_NLN) total++;
    time(&now);
    for (g = msn.GL.head; g != NULL; g = g->next) {
        if (lines >= msn.flskip && lines < total + msn.flskip) {
            wattrset(w_lst.wh, attrs[C_GRP]);
            wprintw(w_lst.wh, "%s\n", g->name);
        }
        lines++;
        for (p = msn.FL.head; p != NULL; p = p->next)
            if (p->gid == g->gid) {
                if (Config.online_only && p->status < MS_NLN) continue;
                if (lines >= msn.flskip && lines < total + msn.flskip) {
                    wattrset(w_lst.wh, lstatattrs[p->status]);
                    if (now - p->tm_last_char <= Config.time_user_types) ch = '!';
                    else if (p->blocked) ch = '+';
                    else if (p->ignored) ch = ':';
                    else ch = ' ';
                    wprintw(w_lst.wh, "%c%c %s\n", ch, msn_stat_char[p->status], 
                            getnick(p->login,p->nick, Config.aliases));
                }
                lines++;
            }
    }
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
            wattrset(w->wh, attrs[C_GRP]);
            wprintw(w->wh, "[%s]\n\n", getnick(sb->master, sb->mnick, Config.aliases));
        }
        lines++;
        for (p = sb->PL.head; p != NULL; p = p->next) {
            if (lines >= sb->plskip && lines < total + sb->plskip) {
                wattrset(w->wh, lstatattrs[MS_UNK]);
                wprintw(w->wh, "  %s\n", getnick(p->login, p->nick, Config.aliases));
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
    attr = attrs[C_NORMAL];
    
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

void dlg(cattr_t attr, const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vdlg(attr, SXL, fmt, ap);
    va_end(ap);
}

void dlgn(cattr_t attr, int size, const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vdlg(attr, size, fmt, ap);
    va_end(ap);
}


/* switchboard conversation window */
void vdlg(cattr_t attr, int size, const char *fmt, va_list ap)
{
    msn_sboard_t *sb = (msn_sboard_t *) pthread_getspecific(key_sboard);
    int y, x, newtop;

    if (sb == NULL) return;
    vwmsg(&sb->w_dlg, size, &sb->dlg_now, &sb->real_dlg_now, attr, sb->fp_log, fmt, ap);
    
    getyx(sb->w_dlg.wh, y, x);
    newtop = y - sb->w_dlg.h + 1;
    if (sb->w_dlg_top < newtop) sb->w_dlg_top = newtop;
    if (sb->w_dlg_top < 0) sb->w_dlg_top = 0;

    draw_dlg(sb, 0);
    draw_sbbar(1);
}

void draw_sbebox(msn_sboard_t *sb, int r)
{
    if (wvis != 0) return;
    if (sb != SL.head) return;
    
    move(w_msg.y - 2, 0);
    if (sb != NULL) {
        attrset(attrs[C_EBX]);
        eb_draw(&sb->EB, stdscr);
    } else {
        attrset(attrs[C_NORMAL]);
        printw("%*c", w_msg.w, ' ');
        move(SLINES - 1, SCOLS - 1);
    }
    attrset(attrs[C_NORMAL]);
    
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
                attrs[C_ERR] = COLOR_PAIR(1);

        init_pair(2, COLOR_GREEN, COLOR_BG);
        lstatattrs[MS_NLN] = attrs[C_DBG] = COLOR_PAIR(2);

        init_pair(3, COLOR_CYAN, COLOR_BG);
        lstatattrs[MS_BRB] = attrs[C_MSG] = COLOR_PAIR(3);

        init_pair(4, COLOR_WHITE, COLOR_RED);
        statattrs[MS_FLN] = COLOR_PAIR(4);

        init_pair(5, COLOR_BLUE, COLOR_CYAN);
        statattrs[MS_NLN] = attrs[C_MNU] = COLOR_PAIR(5);
        statattrs[MS_HDN] = attrs[C_EBX] = COLOR_PAIR(5) | A_REVERSE;

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
        attrs[C_GRP] = COLOR_PAIR(11);
        
        init_pair(12, COLOR_WHITE, COLOR_BG);
        attrs[C_NORMAL] = lstatattrs[MS_UNK] = COLOR_PAIR(12);
    }
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    scrollok(stdscr, TRUE);
    attrset(attrs[C_NORMAL]);

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

    pthread_mutex_init(&w_back.lock, NULL);

    pthread_mutex_init(&scr_lock, NULL);
    
    fullscreen = 1;
    while ((q = queue_get(&msg_q)) != NULL) {
        msg(C_ERR, "%s", q->data);
        free(q->data);
    }
}
