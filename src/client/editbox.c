/*
 *    editbox.c
 *
 *    editbox control for curses
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

#include<stdlib.h>
#include<string.h>
#include<wchar.h>
#include<curses.h>
#include<assert.h>

#include"editbox.h"
#include"utf8.h"

/* clipboard length */
#define CBL 1024

/* history length */
#define EBHLEN 128

static wchar_t clipboard[CBL] = {0};

void eb_mkwtext(ebox_t *e)
{
    mbstate_t mbs;
    int i;
    const char *s;
    
    if (e->utf8) {
        memset(&mbs, 0, sizeof(mbs));
        s = e->text;
        mbsrtowcs(e->wtext, &s, e->nc+1, &mbs);
    } else {
        i = 0;
        while (e->text[i]) e->wtext[i] = (wchar_t) e->text[i], i++;
        e->wtext[i] = (wchar_t) e->text[i];
    }
}

void eb_flush(ebox_t *e)
{
    mbstate_t mbs;
    int i;
    const wchar_t *s;
    
    if (e->utf8) {
        memset(&mbs, 0, sizeof(mbs));
        s = e->wtext;
        wcsrtombs(e->text, &s, e->nb, &mbs);
        e->text[e->nb+1] = 0;
    } else {
        for (i = 0; i < e->sl; i++) e->text[i] = (unsigned char) e->wtext[i];
        e->text[i] = (unsigned char) 0;
    }
}

/* does not store \0 ! */
int eb_mblen(ebox_t *e, wchar_t w, char *dest)
{
    mbstate_t mbs;
    char s[8];
    
    if (dest == NULL) dest = s;
    if (!e->utf8) {
        dest[0] = (unsigned char) w;
        return 1;
    }
    memset(&mbs, 0, sizeof(mbs));
    return wcrtomb(dest, w, &mbs);
}

void eb_init(ebox_t *e, int nc, int width)
{
    e->nc = nc;
    e->nb = 6*nc;
    e->insertmode = 1;
    e->width = width;
    e->ii = e->sl = e->bl = 0;
    e->left = 0;
    e->mask = 0;
    e->esc = 0;
    e->kb = 0;
    e->history = 0;
    e->utf8 = 0;
    e->multi = 0;
    e->mbseq[0] = e->mbseq[1] = e->mbseq[2] = 
    e->mbseq[3] = e->mbseq[4] = e->mbseq[5] = e->mbseq[6] = 0;
    e->mbseqp = &e->mbseq[0];
    hlist_init(&e->HL, EBHLEN);
    e->mline = 0;
    e->grow = 0;
    e->text = (char *) malloc(e->nb+1);
    e->text[0] = 0;
    assert(e->text != NULL);
    e->wtext = (wchar_t *) malloc((e->nc+1)*sizeof(wchar_t));
    e->wtext[0] = 0;
    assert(e->wtext != NULL);
}

void _eb_grow(ebox_t *e)
{
    e->nc *= 2;
    e->nb = 6*e->nc;
    e->text = (char *) realloc(e->text, e->nb+1);
    e->wtext = (wchar_t *) realloc(e->wtext, (e->nc+1)*sizeof(wchar_t));
}

void eb_growfor(ebox_t *e, size_t cap)
{
    if (e->grow) while (e->nc <= cap) _eb_grow(e);
}


void eb_free(ebox_t *e)
{
    hlist_free(&e->HL);
    if (e->text != NULL) free(e->text);
    if (e->wtext != NULL) free(e->wtext);
}

void eb_settext(ebox_t *e, char *s)
{
    if (e->grow) eb_growfor(e,strlen(s));
    memset(e->text, 0, e->nb+1);
    strncpy(e->text, s, e->nb);
    eb_mkwtext(e);
    e->sl = e->bl = 0;
    while (e->wtext[e->sl]) e->sl++;
    e->bl = strlen(e->text);
    e->ii = e->sl;
    if (e->sl > e->nc) e->ii = e->sl = e->nc;
    e->wtext[e->nc] = 0;
    e->left = 0;
    if (e->utf8) {
        e->multi = 0;
        e->mbseq[0] = e->mbseq[1] = e->mbseq[2] = 
        e->mbseq[3] = e->mbseq[4] = e->mbseq[5] = e->mbseq[6] = 0;
        e->mbseqp = &e->mbseq[0];
    }
    eb_flush(e);
}

int eb_pastechar(ebox_t *e, wchar_t c)
{
    int res = 0;
    eb_mkwtext(e);
    if (e->insertmode) {
        if (e->sl < e->nc && e->bl+eb_mblen(e, c, NULL) <= e->nb) {
            if (e->ii < e->sl) wmemmove(e->wtext+e->ii+1, e->wtext+e->ii, e->sl-e->ii);
            e->wtext[e->ii++] = c;
            e->wtext[++e->sl] = 0;
            e->bl += eb_mblen(e, c, NULL);
            eb_growfor(e,e->sl);
            res = 1;
        }
    } else if (e->ii < e->nc) {
        int newbl;
        if (e->ii < e->sl) newbl = e->bl - eb_mblen(e, e->wtext[e->ii], NULL) + eb_mblen(e, c, NULL);
        else newbl = e->bl + eb_mblen(e, c, NULL);
        if (newbl <= e->nb) {
            e->wtext[e->ii++] = c;
            e->bl = newbl;
            if (e->ii > e->sl) e->wtext[++e->sl] = 0;
            eb_growfor(e,e->sl);
            res = 1;
        }
    }
    eb_flush(e);
    return res;
}

void eb_history_add(ebox_t *e, char *s, int len)
{
    if (!e->history) return;
    hlist_add(&e->HL, s, len, -1);
}

void eb_zap(ebox_t *e)
{
    e->esc = 0;
    wmemset(e->wtext, 0, e->nc);
    e->sl = e->ii = e->bl = 0;
    eb_flush(e);
}

void eb_cut(ebox_t *e)
{
    e->esc = 0;
    if (e->mask) return;
    wcsncpy(clipboard, e->wtext, CBL);
    clipboard[CBL-1] = 0;
    wmemset(e->wtext, 0, e->nc);
    e->sl = e->ii = 0;
    eb_flush(e);
    e->bl = strlen(e->text);
}

int eb_keydown(ebox_t *e, int key)
{
    switch (key) {
        case KEY_UP: /* history previous */
            if (e->history && e->HL.head != NULL) {
                if (strcmp(e->HL.head->text, e->text) == 0) e->HL.head = e->HL.head->prev;
                eb_settext(e, e->HL.head->text);
                e->HL.head = e->HL.head->prev;
            }
            break;
        case KEY_DOWN: /* history next */
            if (e->history && e->HL.head != NULL) {
                e->HL.head = e->HL.head->next;
                if (strcmp(e->HL.head->text, e->text) == 0) e->HL.head = e->HL.head->next;
                eb_settext(e, e->HL.head->text);
            }
            break;
        case KEY_LEFT:
            if (e->ii > 0) {
                e->ii--;
            }
            break;
        case KEY_RIGHT:
            if (e->ii < e->sl) {
                e->ii++;
            }
            break;
        case KEY_HOME:
            if (e->ii > 0) {
                e->ii = 0;
            }
            break;
        case KEY_END:
            if (e->ii < e->sl) {
                e->ii = e->sl;
            }
            break;
        case KEY_IC:
            e->insertmode ^= 1;
            break;
        case KEY_DC:
            if (e->ii < e->sl) {
                eb_mkwtext(e);
                e->bl -= eb_mblen(e, e->wtext[e->ii], NULL);
                wmemmove(e->wtext+e->ii, e->wtext+e->ii+1, e->sl-e->ii);
                e->wtext[--e->sl] = 0;
                eb_flush(e);
            }
            break;
        case 20: /* ^T - delete word */
            if (e->ii < e->sl) {
                while (e->ii < e->sl) {
                    e->bl -= eb_mblen(e, e->wtext[e->ii], NULL);
                    wmemmove(e->wtext+e->ii, e->wtext+e->ii+1, e->sl-e->ii);
                    e->wtext[--e->sl] = 0;
                    eb_flush(e);
                    if (e->wtext[e->ii] < 48) break;
                }
            }
            break;
        case 25: /* ^Y - cut */
            eb_cut(e);
            break;
        case 1: /* ^A - word left */
            if (e->ii > 0) {
                while ((e->ii) && (e->wtext[--e->ii]<48));
                while ((e->ii) && (e->wtext[--e->ii]>=48));
                if (e->ii) e->ii++;
            }
            break;
        case 4: /* ^D - word right */
            if (e->ii < e->sl) {
                while ((e->ii<e->sl) && (e->wtext[e->ii++]>=48));
                while ((e->ii<e->sl) && (e->wtext[e->ii++]<48));
                if (e->ii<e->sl) e->ii--;
            }
            break;
        case 11: /* ^K - clipboard escape */
            e->esc ^= 1;
            break;
        case 8: /* ^H */
        case KEY_BACKSPACE:
            if (e->ii > 0) {
                if (e->ii == e->sl) {
                    e->bl -= eb_mblen(e, e->wtext[--e->ii], NULL);
                    e->sl--; e->wtext[e->ii] = 0;
                } else {
                    wmemmove(e->wtext+e->ii-1, e->wtext+e->ii, e->sl-e->ii);
                    e->ii--; 
                    e->bl -= eb_mblen(e, e->wtext[--e->sl], NULL);
                    e->wtext[e->sl] = 0;
                }
                eb_flush(e);
            }
            break;
        default:
            if (e->esc) switch(key) {
                case 'b':
                case 'B': /* block begin */
                    e->esc = 0;
                    if (e->mask) break;
                    e->kb = e->ii;
                    break;
                case 'k':
                case 'K': /* block end (copy) */
                    e->esc = 0;
                    if (e->mask) break;
                    if (e->ii > e->kb) {
                        int ncpy = e->ii - e->kb;
                        if (ncpy > CBL) ncpy = CBL - 1;
                        wcsncpy(clipboard, e->wtext + e->kb, ncpy);
                        clipboard[ncpy] = 0;
                        clipboard[e->ii - e->kb] = 0;
                    } else clipboard[0] = 0;
                    break;
                case 'x':
                case 'X': /* cut */
                    eb_cut(e);
                    break;
                case 'z':
                case 'Z': /* zap */
                    eb_zap(e);
                    break;
                case 'c':
                case 'C': /* copy */
                    e->esc = 0;
                    if (e->mask) break;
                    wcsncpy(clipboard, e->wtext, CBL);
                    clipboard[CBL-1] = 0;
                    break;
                case 'v':
                case 'V': {/* paste */
                    wchar_t *c;
                    
                    e->esc = 0;
                    for (c = clipboard; *c; c++) eb_pastechar(e, *c);
                    break;
                }
                case 'n':
                case 'N': /* soft-newline */
                    e->esc = 0;
                    eb_pastechar(e, (wchar_t) '\n');
                    break;
                case '\r':
                    e->esc = 0;
                    e->mline ^= 1;
                    if (e->mline == 0) return 0;
                    break;
                default:
                    e->esc = 0;
            } else if ((key >= 32) && (key <= 255)) {
                if (!e->utf8) eb_pastechar(e, (wchar_t) key);
                else { /* utf8 */
                    int multi;
                    mbstate_t mbs;
                    wchar_t wc;
                    
                    multi = seqlen(key);
                    if (multi > 0) { /* first char of mb seq */
                        e->multi = multi - 1;
                        e->mbseqp = &e->mbseq[0];
                        e->mbseq[1] = e->mbseq[2] = e->mbseq[3] = 
                        e->mbseq[4] = e->mbseq[5] = e->mbseq[6] = 0;
                        *(e->mbseqp++) = (unsigned char) key;
                    } else { /* intermediate char of mb seq */
                        e->multi--;
                        *(e->mbseqp++) = (unsigned char) key;
                    }
                    if (e->multi == 0) { /* end of mb seq */
                        memset(&mbs, 0, sizeof(mbs));
                        mbrtowc(&wc, e->mbseq, 7, &mbs);
                        eb_pastechar(e, wc);
                    }
                }
            } else if (e->mline && key == '\r') {
                eb_pastechar(e, (wchar_t) '\n'); 
            } else return 0;
            break;
    }
    return 1;
}

void eb_draw(ebox_t *e, WINDOW *w)
{
    int y, x, i, num_print, num_print_ii;
    wchar_t ws;

    getyx(w, y, x);
    wprintw(w, "%*c", e->width, ' ');
    wmove(w, y, x);
    eb_mkwtext(e);
    num_print = num_print_ii = i = 0;
    if (e->ii > e->left + 8)
        if (e->utf8) 
            while (wstrwidth(&e->wtext[e->left], e->ii-e->left) > e->width-1) e->left++;
        else while ((e->ii-e->left) > e->width-1) e->left++;
    else {
        e->left = e->ii - 8;
        if (e->left < 0) e->left = 0;
    }
    while (num_print < e->width - 1) {
        ws = e->wtext[e->left + i];
        if (ws == 0) break;
        if (ws == '\n') ws = (wchar_t) '|';
        if (e->mask) waddch(w, '*');
        else {
            char s[8] = {0};
            eb_mblen(e, ws, s);
            waddstr(w, s);
        }
        if (e->utf8) num_print += wcwidth_nl(ws);
        else num_print++;
        i++;
        if (e->left + i == e->ii) num_print_ii = num_print;
    }
    wmove(w, y, x + num_print_ii);
}

void eb_resize(ebox_t *e, int nw)
{
    e->width = nw;
}
