/*
 *    sboard.c
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

#include<curses.h>
#include<pthread.h>
#include<stdlib.h>
#include<iconv.h>

#include"sboard.h"
#include"xfer.h"
#include"unotif.h"
#include"gtmess.h"

pthread_mutex_t SX; /* switchboard mutex */
msn_sblist_t SL; /* switchboard */
pthread_key_t key_sboard;

void *Malloc(size_t size);

void draw_dlg(msn_sboard_t *sb, int r)
{
    TWindow *w, data;
    int y;
    
    if (sb == NULL) w = &w_back;
    else {
        w = &sb->w_dlg;
        /*if (sb->pending == 1) unotify(SP, SND_PENDING), sb->pending++;*/
        if (sb == SL.head) sb->pending = 0;
        else if (sb != SL.head && sb->pending) unotify(SP, SND_PENDING);
    }
    if (wvis != 0) return;
    if (sb == SL.head) {
        LOCK(&w->lock);
        data = *w;
        data.wh = newwin(w->h, w->w, 0, 0);
        if (sb != NULL) y = sb->w_dlg_top; else y = 0;
        copywin(w->wh, data.wh, y, 0, 0, 0, w->h-1, w->w-1, FALSE);
        UNLOCK(&w->lock);
        scr_event(SCR_RDLG, &data, sizeof(data), r);
    }
}

/* switchboard participants */
void draw_plst(msn_sboard_t *sb, int r)
{
    msn_contact_t *p;
    TWindow *w, data;
    int lines, total;

    if (wvis != 0) return;
    if (sb == NULL) w = &w_back; else w = &sb->w_prt;
    LOCK(&w->lock);
    wclear(w->wh);

    if (sb != NULL) {
        lines = 0;
        total = sb->PL.count + 1;
        if (lines >= sb->plskip && lines < total + sb->plskip) {
            wattrset(w->wh, attrs[C_GRP]);
            wprintw(w->wh, "[%s]\n\n", sb->mnick);
        }
        lines++;
        for (p = sb->PL.head; p != NULL; p = p->next) {
            if (lines >= sb->plskip && lines < total + sb->plskip) {
                wattrset(w->wh, lstatattrs[MS_UNK]);
                wprintw(w->wh, "  %s\n", p->nick);
            }
            lines++;
        }
    }
    UNLOCK(&w->lock);
    if (sb == SL.head) {
        LOCK(&w->lock);
        data = *w;
        data.wh = dupwin(w->wh);
        UNLOCK(&w->lock);
        scr_event(SCR_RPLST, &data, sizeof(data), r);
    }
}

void draw_sbbar(int r)
{
    int i, c;
    int left, leftp, right, rightp;
    msn_sboard_t *s;
    scrtxt_t data;
    char *ch;
    
    data.attr = attrs[C_NORMAL];
    ch = &data.txt[0];
    
    s = SL.start;
    c = SL.count;
    if (c < w_msg.w) {
        for (i = 0; i < c; i++, s = s->next) {
            if (s == SL.head) *ch++ = 'O';
            else if (s->pending) *ch++ = '+';
            else *ch++ = '-';
        }
        *ch = 0;
    } else {
        left = leftp = right = rightp = 0;
        for (left = 0; s != SL.head; s = s->next, left++) if (s->pending) leftp++;
        s = s->next;
        for (right = 0; s != SL.start; s = s->next, right++) if (s->pending) right++;
        sprintf(ch, "(+%d/%d)[%d](+%d/%d)", leftp, left, c, rightp, right);
    }
    
    scr_event(SCR_SBBAR, &data, sizeof(data), r);
}

/* switchboard conversation window */
void dlg(cattr_t attr, const char *fmt, ...)
{
    msn_sboard_t *sb = (msn_sboard_t *) pthread_getspecific(key_sboard);
    int y, x, newtop;
    va_list ap;

    if (sb == NULL) return;
    va_start(ap, fmt);
    vwmsg(&sb->w_dlg, attr, fmt, ap);
    va_end(ap);
    
    getyx(sb->w_dlg.wh, y, x);
    newtop = y - sb->w_dlg.h + 1;
    if (sb->w_dlg_top < newtop) sb->w_dlg_top = newtop;
    if (sb->w_dlg_top < 0) sb->w_dlg_top = 0;

    draw_dlg(sb, 0);
    draw_sbbar(1);
}

void draw_sbebox(msn_sboard_t *sb, int r)
{
    scrsbebx_t data;

    if (wvis != 0) return;
    if (sb != SL.head) return;
    if (sb != NULL) {
        data.null = 0;
        data.attr = attrs[C_EBX];
        data.ebox = sb->EB;
        strcpy(data.txt, sb->EB.text);
    } else data.null = 1;
    scr_event(SCR_SBEBDRAW, &data, sizeof(data), r);
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

msn_sboard_t *msn_sblist_add(msn_sblist_t *q, char *sbaddr, char *hash,
                             char *master, char *mnick, int called, char *sessid)
{
    msn_sboard_t *p;

    p = (msn_sboard_t *) Malloc(sizeof(msn_sboard_t));
    strcpy(p->sbaddr, sbaddr);
    strcpy(p->hash, hash);
    p->called = called;

    p->sessid[0] = 0;
    if (sessid != NULL) strcpy(p->sessid, sessid);
    p->PL.head = NULL; p->PL.count = 0;
    strcpy(p->master, master);
    strcpy(p->mnick, mnick);
    p->multi = 0;
    p->cantype = 0;

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

    p->ic = iconv_open(Config.console_encoding, "UTF-8");
    p->tid = 0;
    p->sfd = -1;
    p->thrid = -1;
    p->input[0] = 0;
    eb_init(&p->EB, SXL - 1, w_msg.w, p->input);
    p->EB.history = 1;
/*    eb_settext(&p->EB, SP);*/
    p->pending = 0;
    p->tm_last_char = 0;
    p->plskip = 0;

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
        pthread_cancel(p->thrid);
        pthread_join(p->thrid, NULL);
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
    hlist_free(&p->EB.HL);
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
