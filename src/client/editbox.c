/*
 *    editbox.c
 *
 *    editbox control for curses
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

#include<stdlib.h>
#include<string.h>
#include<curses.h>
#include"editbox.h"

/* clipboard length */
#define CBL 256

/* history length */
#define EBHLEN 128

static char clipboard[CBL] = {0};

void eb_init(ebox_t *e, int nc, int width, char *s)
{
    e->nc = nc;
    e->insertmode = 1;
    e->text = s;
    e->width = width;
    e->ii = e->sl = e->left = 0;
    e->mask = 0;
    e->esc = 0;
    e->kb = 0;
    e->history = 0;
    hlist_init(&e->HL, EBHLEN);
}

void eb_settext(ebox_t *e, char *s)
{
    e->ii = e->sl = strlen(s);
    if (e->sl > e->nc) e->ii = e->sl = e->nc;
    if (e->text != s) {
        memset(e->text, 0, e->nc);
        strncpy(e->text, s, e->nc);
    }
    e->text[e->nc] = 0;
    e->left = (e->width > e->sl) ? 0: e->sl - e->width + 1;
}

void eb_pastechar(ebox_t *e, int c)
{
    if (e->insertmode) {
        if (e->sl < e->nc) {
            if (e->ii < e->sl) memmove(e->text+e->ii+1, e->text+e->ii, e->sl-e->ii);
            e->text[e->ii++] = (char) c;
            e->text[++e->sl] = 0;
            if (e->ii-e->left >= e->width) e->left++;
        }
    } else if (e->ii < e->nc) {
        e->text[e->ii++] = (char) c;
        if (e->ii > e->sl) e->text[++e->sl] = 0;
        if (e->ii-e->left >= e->width) e->left++;
    }
}

void eb_history_add(ebox_t *e, char *s, int len)
{
    if (!e->history) return;
    hlist_add(&e->HL, s, len, -1);
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
                if (e->left > e->ii) e->left--;
            }
            break;
        case KEY_RIGHT:
            if (e->ii < e->sl) {
                e->ii++;
                if (e->ii-e->left >= e->width) e->left++;
            }
            break;
        case KEY_HOME:
            if (e->ii > 0) {
                e->ii = e->left = 0;
            }
            break;
        case KEY_END:
            if (e->ii < e->sl) {
                e->ii = e->sl;
                e->left = (e->width > e->sl) ? 0 : e->sl - e->width + 1;
            }
            break;
        case KEY_IC:
            e->insertmode ^= 1;
            break;
        case KEY_DC:
            if (e->ii < e->sl) {
                memmove(e->text+e->ii, e->text+e->ii+1, e->sl-e->ii);
                e->text[--e->sl] = 0;
                if ((e->sl - e->left) < (e->width-1) && e->left > 0) e->left--;
            }
            break;
        case 20: /* ^T - delete word */
            if (e->ii < e->sl) {
                while (e->ii < e->sl) {
                    memmove(e->text+e->ii, e->text+e->ii+1, e->sl-e->ii);
                    e->text[--e->sl] = 0;
                    if ((e->sl - e->left) < (e->width-1) && e->left > 0) e->left--;
                    if (e->text[e->ii] < 48) break;
                }
            }
            break;
        case 1: /* ^A - word left */
            if (e->ii > 0) {
                while ((e->ii) && (e->text[--e->ii]<48));
                while ((e->ii) && (e->text[--e->ii]>=48));
                if (e->ii) e->ii++;
                if (e->left > e->ii) e->left = e->ii;
            }
            break;
        case 4: /* ^D - word right */
            if (e->ii < e->sl) {
                while ((e->ii<e->sl) && (e->text[e->ii++]>=48));
                while ((e->ii<e->sl) && (e->text[e->ii++]<48));
                if (e->ii<e->sl) e->ii--;
                if (e->ii-e->left >= e->width) e->left = e->ii - e->width + 1;
            }
            break;
        case 11: /* ^K - clipboard escape */
            e->esc ^= 1;
            break;
        case 8: /* ^H */
        case KEY_BACKSPACE:
            if (e->ii > 0) {
                if (e->ii == e->sl) {
                    e->sl--; e->text[--e->ii] = 0;
                } else {
                    memmove(e->text+e->ii-1, e->text+e->ii, e->sl-e->ii);
                    e->ii--; e->text[--e->sl] = 0;
                }
                if (e->left > 0) e->left--;
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
                        strncpy(clipboard, e->text + e->kb, ncpy);
                        clipboard[ncpy] = 0;
                        clipboard[e->ii - e->kb] = 0;
                    } else clipboard[0] = 0;
                    break;
                case 'x':
                case 'X': /* cut */
                    e->esc = 0;
                    if (e->mask) break;
                    strncpy(clipboard, e->text, CBL);
                    clipboard[CBL-1] = 0;
                    memset(e->text, 0, e->nc);
                    e->sl = e->ii = e->left = 0;
                    break;
                case 'z':
                case 'Z': /* zap */
                    e->esc = 0;
                    memset(e->text, 0, e->nc);
                    e->sl = e->ii = e->left = 0;
                    break;
                case 'c':
                case 'C': /* copy */
                    e->esc = 0;
                    if (e->mask) break;
                    strncpy(clipboard, e->text, CBL);
                    clipboard[CBL-1] = 0;
                    break;
                case 'v':
                case 'V': {/* paste */
                    char *c;
                    
                    e->esc = 0;
                    for (c = clipboard; *c; c++) eb_pastechar(e, *c);
                    break;
                }
                default:
                    e->esc = 0;
            } else if ((key >= 32) && (key <= 255)) eb_pastechar(e, key);
            else return 0;
            break;
    }
    return 1;
}

void eb_draw(ebox_t *e, WINDOW *w)
{
    int y, x, i, c;

    getyx(w, y, x);
    wprintw(w, "%*c", e->width, ' ');
    wmove(w, y, x);
    for (i = 0; i < e->width; i++) {
        c = e->text[e->left + i];
        if (c == 0) break;
        if (!e->mask) waddch(w, c); else waddch(w, '*');
    }
    wmove(w, y, x + e->ii - e->left);
}

void eb_resize(ebox_t *e, int nw)
{
    e->width = nw;
    if (e->ii-e->width >= e->left)
        e->left = e->ii - e->width + 1;
}
