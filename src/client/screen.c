/*
 *    screen.c
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

#include<pthread.h>
#include<string.h>
#include<stdlib.h>
#include<ctype.h>
#include<time.h>

#include"screen.h"
#include"queue.h"
#include"xfer.h"
#include"gtmess.h"

int attrs[] = {A_NORMAL, A_UNDERLINE, A_BOLD, A_NORMAL, A_STANDOUT, A_REVERSE, A_NORMAL};
int statattrs[] = {A_REVERSE, A_REVERSE, A_STANDOUT, A_REVERSE,
        A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE, A_REVERSE};
int lstatattrs[] = {A_NORMAL, A_NORMAL, A_REVERSE, A_BOLD, A_BOLD,
        A_BOLD | A_UNDERLINE, A_BOLD, A_BOLD | A_UNDERLINE, A_BOLD, A_STANDOUT};

TWindow w_msg, w_lst, w_back, w_xfer;
pthread_mutex_t scr_lock;

int wvis; /* 0 = sb window visible, 1 = transfers window visible */
pthread_mutex_t WL;
int w_msg_top = 0;

typedef struct {
    pthread_cond_t cond;
    pthread_t thrid;
    
    queue_t evq;    
} screen_t;

typedef struct {
    TWindow *w;
    cattr_t attr;
    char txt[SML];
} scrmsg_t;

screen_t Screen;    

int getword(char *dest, char *src, int max)
{
    char *word, *wb;
    int wl;
    
    wl = 0;
    word = wb = dest;
    while (*src) {
        *wb = *src++;
        wl++;
        if (isspace(*wb)) break;
        if (wl >= max) break;
        wb++;
    }
    word[wl] = 0;
    return wl;
}

void wprintx(WINDOW *w, char *txt)
{
    int mx, my, len;
    char *word, wl;
    
    
    getmaxyx(w, my, mx);
    mx--;
    len = 0;
    word = strdup(txt);
    while ((wl = getword(word, txt, mx)) > 0) {
        if (len + wl > mx) {
            waddch(w, '\n');
            len = 0;
        }
        waddstr(w, word);
        txt += wl;
        len += wl;
    }
    free(word);
}


void evd_sbbar(scrtxt_t *e)
{
    int y, x, i;
    
    getyx(stdscr, y, x);
    move(w_msg.y - 1, 0);
    for (i = 0; i < w_msg.w; i++) addch(ACS_HLINE);
    move(w_msg.y - 1, 0);
    attrset(e->attr);
    addstr(e->txt);
    move(y, x);
}

void evd_sbebdraw(scrsbebx_t *e)
{
    move(w_msg.y - 2, 0);
    if (!e->null) {
        attrset(e->attr);
        e->ebox.text = e->txt;
        eb_draw(&e->ebox, stdscr);
    } else {
        attrset(attrs[C_NORMAL]);
        printw("%*c", w_msg.w, ' ');
        move(LINES - 1, COLS - 1);
    }
    attrset(attrs[C_NORMAL]);
}

void evd_rplst(TWindow *w)
{
    int y, x;
    
    getyx(stdscr, y, x);
    copywin(w->wh, stdscr, 0, 0, W_PRT_Y, W_PRT_X,
            W_PRT_Y + W_PRT_H - 1, W_PRT_X + W_PRT_W - 1, FALSE);
    move(y, x);
    delwin(w->wh);
}

void evd_rdlg(TWindow *w)
{
    int y, x;

    getyx(stdscr, y, x);
    copywin(w->wh, stdscr, 0, 0, W_DLG_Y, W_DLG_X,
            W_DLG_Y + W_DLG_H - 1, W_DLG_X + W_DLG_W - 1, FALSE);
    move(y, x);
    delwin(w->wh);
}

/* print a message */
void vwmsg(TWindow *w, cattr_t attr, const char *fmt, va_list ap)
{
    char tmp[SXL], *s;

    vsprintf(tmp, fmt, ap);

    for (s = tmp; *s; s++) if (*s == '\r') *s = ' ';

    LOCK(&w->lock);
    wattrset(w->wh, attrs[attr]);
/*    waddstr(w->wh, tmp);*/
    wprintx(w->wh, tmp);
    wattrset(w->wh, attrs[C_NORMAL]);
    UNLOCK(&w->lock);
}

int get_string(cattr_t attr, int mask, const char *prompt, char *dest)
{
    int r;
    char s[SML];
    int c, pl;
    ebox_t E;
    screbx_t data;

    strncpy(s, dest, SML);
    s[SML - 1] = 0;
    pl = strlen(prompt);
    eb_init(&E, SML-1, COLS-1-pl, s);
    E.mask = mask;
    eb_settext(&E, s);
    c = -1;
    strcpy(data.prompt, prompt);
    data.pl = pl;
    data.attr = attrs[attr];
    scr_event(SCR_EBINIT, &data, sizeof(data), 0);
    while (1) {
        if (c == 13 || c == 27) break;
        data.ebox = E;
        strcpy(data.txt, E.text);
        scr_event(SCR_EBDRAW, &data, sizeof(data), 1);
        c = getch();
        eb_keydown(&E, c);
    }
    r = (c == 13);
    if (r) {
        if (strcmp(dest, s) == 0) r <<= 1;
        strcpy(dest, s);
    }
    scr_event(SCR_HOME, NULL, 0, 1);
    return r;
}

void evd_lst(scrlst_t *e)
{
    msn_contact_t *p;
    msn_group_t *g;
    time_t now;
    char ch;
    int lines, total;

    LOCK(&w_lst.lock);
    wclear(w_lst.wh);

    lines = 0;
    total = e->g.count;
    if (Config.online_only == 0) total += e->p.count;
    else 
        for (p = e->p.head; p != NULL; p = p->next)
            if (p->status >= MS_NLN) total++;
    time(&now);
    for (g = e->g.head; g != NULL; g = g->next) {
        if (lines >= e->skip && lines < total + e->skip) {
            wattrset(w_lst.wh, attrs[C_GRP]);
            wprintw(w_lst.wh, "%s\n", g->name);
        }
        lines++;
        for (p = e->p.head; p != NULL; p = p->next)
            if (p->gid == g->gid) {
                if (Config.online_only && p->status < MS_NLN) continue;
                if (lines >= e->skip && lines < total + e->skip) {
                    wattrset(w_lst.wh, lstatattrs[p->status]);
                    if (now - p->tm_last_char <= Config.time_user_types) ch = '!';
                    else if (p->blocked) ch = '+';
                    else ch = ' ';
                    wprintw(w_lst.wh, "%c%c %s\n", ch, msn_stat_char[p->status], p->nick);
                }
                lines++;
            }
    }
    msn_clist_free(&e->p);
    msn_glist_free(&e->g);
    UNLOCK(&w_lst.lock);
}

void evd_rlst()
{
    int y, x;
    
    getyx(stdscr, y, x);
    LOCK(&w_lst.lock);
    copywin(w_lst.wh, stdscr, 0, 0, w_lst.y, w_lst.x,
            w_lst.y + w_lst.h - 1, w_lst.x + w_lst.w - 1, FALSE);
    UNLOCK(&w_lst.lock);
    move(y, x);
}

void evd_home()
{
    move(LINES - 1, COLS - 1);
}

void evd_ebdraw(screbx_t *e)
{
    move(LINES - 1, e->pl);
    attrset(e->attr);
    e->ebox.text = e->txt;
    eb_draw(&e->ebox, stdscr);
    attrset(attrs[C_NORMAL]);
}

void evd_ebinit(screbx_t *e)
{
    move(LINES - 1, 0);
    bkgdset(' ' | e->attr);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);
    move(LINES - 1, 0);
    attrset(e->attr ^ A_REVERSE);
    addstr(e->prompt);
    attrset(attrs[C_NORMAL]);
}

void evd_topline(scrtxt_t *e)
{
    int y, x;

    getyx(stdscr, y, x);
    move(0, 0);
    bkgdset(' ' | e->attr);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);
    attrset(e->attr);
    addstr(e->txt);
/*    addch(' ');
    addch(ACS_VLINE); 
    printw(" gtmess %s", VERSION);*/
    attrset(attrs[C_NORMAL]);
    move(y, x);
}

void msg2(cattr_t attr, const char *fmt, ...)
{
    va_list ap;
    scrtxt_t data;

    va_start(ap, fmt);
    vsprintf(data.txt, fmt, ap);
    va_end(ap);
    data.attr = attr;
    
    scr_event(SCR_BOTLINE, &data, sizeof(data), 1);
}

void evd_botline(scrtxt_t *e)
{
    int y, x;
    
    e->txt[COLS-1] = 0;
    getyx(stdscr, y, x);
    move(LINES - 1, 0);
    bkgdset(' ' | attrs[e->attr]);
    clrtoeol();
    bkgdset(' ' | attrs[C_NORMAL]);
    attrset(attrs[e->attr]);
    addstr(e->txt);
    attrset(attrs[C_NORMAL]);
    move(y, x);
}

void scr_vmsg(TWindow *w, cattr_t attr, const char *fmt, va_list ap)
{
    scrmsg_t data;
    char *s;

    vsprintf(data.txt, fmt, ap);
    data.w = w;
    data.attr = attr;

    for (s = data.txt; *s; s++) if (*s == '\r') *s = ' ';
    
    scr_event(SCR_MSG, &data, sizeof(data), 0);
}

/* message window */
void msg(cattr_t attr, const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    scr_vmsg(&w_msg, attr, fmt, ap);
    va_end(ap);
    scr_event(SCR_RMSG, &w_msg, 0, 1);
}

/*void write_msg(scrmsg_t *e, ...)
{
    va_list ap;
    
    va_start(ap, e);
    vwmsg(e->w, e->attr, "%s", ap);
    va_end(ap);
}*/

void write_msg(scrmsg_t *e)
{
    int y, x, newtop;
    
    LOCK(&e->w->lock);
    wattrset(e->w->wh, attrs[e->attr]);
    waddstr(e->w->wh, e->txt);
    wattrset(e->w->wh, attrs[C_NORMAL]);
    getyx(e->w->wh, y, x);
    newtop = y - e->w->h + 1;
    if (w_msg_top < newtop) w_msg_top = newtop;
    if (w_msg_top < 0) w_msg_top = 0;
    UNLOCK(&e->w->lock);
}

    
void refresh_msg(TWindow *w)
{
    int y, x;
    
    getyx(stdscr, y, x);
    LOCK(&w->lock);
/*      prefresh(w->wh, 2, 0, w->y, w->x,
              w->y + w->h - 1, w->x + w->w - 1);*/
      copywin(w->wh, stdscr, w_msg_top, 0, w->y, w->x,
              w->y + w->h - 1, w->x + w->w - 1, FALSE);
    UNLOCK(&w->lock);
    move(y, x);
}        

void scr_event(int type, void *data, int size, int signal)
{
    pthread_mutex_lock(&scr_lock);
    if (queue_put(&Screen.evq, type, data, size) == 0 && signal)
        pthread_cond_signal(&Screen.cond);
    pthread_mutex_unlock(&scr_lock);
}

void evd_rest()
{
    int y, x;

    getyx(stdscr, y, x);
    move(1, COLS * 3 / 4);
    vline(ACS_VLINE, LINES - 2);
    mvaddch(w_msg.y - 1, COLS * 3 / 4, ACS_RTEE);
    move(y, x);
}

void evd_xfer()
{
/*    int y, x;
    
    getyx(stdscr, y, x);*/
    LOCK(&w_xfer.lock);
        copywin(w_xfer.wh, stdscr, 0, 0, w_xfer.y, w_xfer.x,
                w_xfer.y + w_xfer.h - 1, w_xfer.x + w_xfer.w - 1, FALSE);
    UNLOCK(&w_xfer.lock);
    move(LINES - 1, COLS - 1);
/*    move(y, x);*/
}

void *screen_daemon(void *dummy)
{
    qelem_t *e;
    int r = 0;
    
    pthread_mutex_lock(&scr_lock);
    
    while (1) {
        pthread_cond_wait(&Screen.cond, &scr_lock);
        r = Screen.evq.count > 0;
        
        while ((e = queue_get(&Screen.evq)) != NULL) {
            switch (e->type) {
                case SCR_DRAWREST: evd_rest(); break;
                case SCR_MSG: write_msg(e->data); break;
                case SCR_RMSG: refresh_msg(e->data); break;
                case SCR_TOPLINE: evd_topline(e->data); break;
                case SCR_BOTLINE: evd_botline(e->data); break;
                case SCR_EBINIT: evd_ebinit(e->data); break;
                case SCR_EBDRAW: evd_ebdraw(e->data); break;
                case SCR_HOME: evd_home(); break;
                case SCR_LST: evd_lst(e->data); break;
                case SCR_RLST: evd_rlst(); break;
                
                case SCR_RDLG: evd_rdlg(e->data); break;
                case SCR_RPLST: evd_rplst(e->data); break;
                case SCR_SBEBDRAW: evd_sbebdraw(e->data); break;
                case SCR_SBBAR: evd_sbbar(e->data); break;
                
                case SCR_XFER: evd_xfer(); break;
            }
                    
            qelem_free(e);
        }
        
        if (r) refresh();
    }
}

void screen_init(int colors)
{
    initscr();
    if (colors == 1 || (colors == 0 && has_colors())) {
        start_color();
        init_pair(1, COLOR_RED, COLOR_BLACK);
        lstatattrs[MS_FLN] = lstatattrs[MS_HDN] =
                attrs[C_ERR] = COLOR_PAIR(1);

        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        lstatattrs[MS_NLN] = attrs[C_DBG] = COLOR_PAIR(2);

        init_pair(3, COLOR_CYAN, COLOR_BLACK);
        lstatattrs[MS_BRB] = attrs[C_MSG] = COLOR_PAIR(3);

        init_pair(4, COLOR_WHITE, COLOR_RED);
        statattrs[MS_FLN] = COLOR_PAIR(4);

        init_pair(5, COLOR_BLUE, COLOR_CYAN);
        statattrs[MS_NLN] = attrs[C_MNU] = COLOR_PAIR(5);
        statattrs[MS_HDN] = attrs[C_EBX] = COLOR_PAIR(5) | A_REVERSE;

        init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
        lstatattrs[MS_BSY] = lstatattrs[MS_PHN] = COLOR_PAIR(6);

        init_pair(7, COLOR_BLACK, COLOR_CYAN);
        statattrs[MS_IDL] = statattrs[MS_AWY] =
                statattrs[MS_LUN] = COLOR_PAIR(7);

        init_pair(8, COLOR_WHITE, COLOR_MAGENTA);
        statattrs[MS_BSY] = statattrs[MS_PHN] = COLOR_PAIR(8);

        init_pair(9, COLOR_BLACK, COLOR_GREEN);
        statattrs[MS_BRB] = COLOR_PAIR(9);

        init_pair(10, COLOR_YELLOW, COLOR_BLACK);
        lstatattrs[MS_IDL] = lstatattrs[MS_AWY] =
                lstatattrs[MS_LUN] = COLOR_PAIR(10) | A_BOLD;

        init_pair(11, COLOR_BLUE, COLOR_BLACK);
        attrs[C_GRP] = COLOR_PAIR(11);
        
        init_pair(12, COLOR_WHITE, COLOR_BLACK);
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
    move(LINES - 1, COLS - 1);
    refresh();

    w_msg.w = COLS * 3 / 4;
    w_msg.h = 2 * LINES / 5 - 2;
    w_msg.wh = newpad(W_MSG_LBUF, w_msg.w);
/*    w_msg.wh = newwin(w_msg.h, w_msg.w, 0, 0);*/
    scrollok(w_msg.wh, TRUE);
    w_msg.x = 0;
    w_msg.y = LINES - 1 - w_msg.h;
    pthread_mutex_init(&w_msg.lock, NULL);
    
    w_xfer.h = W_DLG_H + 1;
    w_xfer.w = W_DLG_W + W_PRT_W;
    w_xfer.x = W_DLG_X;
    w_xfer.y = W_DLG_Y;
    w_xfer.wh = newwin(w_xfer.h, w_xfer.w, 0, 0);
    pthread_mutex_init(&w_xfer.lock, NULL);

    w_lst.w = COLS - w_msg.w - 1;
    w_lst.h = LINES - 2;
    w_lst.wh = newwin(w_lst.h, w_lst.w, 0, 0);
    scrollok(w_lst.wh, TRUE);
    w_lst.x = w_msg.x + w_msg.w + 1;
    w_lst.y = 1;
    pthread_mutex_init(&w_lst.lock, NULL);

    w_back.w = COLS;
    w_back.h = LINES;
    w_back.wh = newwin(w_back.h, w_back.w, 0, 0);
    w_back.x = 0;
    w_back.y = 0;
    
    wvis = 0;

    pthread_mutex_init(&w_back.lock, NULL);

    pthread_mutex_init(&scr_lock, NULL);
    
    pthread_mutex_init(&WL, NULL);

    pthread_cond_init(&Screen.cond, NULL);
    
    pthread_create(&Screen.thrid, NULL, screen_daemon, NULL);
    pthread_detach(Screen.thrid);
}

