/*
 *    menu.c
 *
 *    menu control for curses
 *    Copyright (C) 2006-2007  George M. Tzoumas
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

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <curses.h>


#include "menu.h"
#include "screen.h"

void mn_calc_size(menu_t *m, WINDOW *w)
{
    int tmp;
    if (m->msz == -1 && w != NULL) {
    	getmaxyx(w, tmp, m->ms);
        m->ms--;
    } else {
        m->ms = m->msz;
    }
    m->wrap = (m->item[m->n].offset <= m->ms);
}

void mn_init(menu_t *m, int size, int msz, ...)
{
    va_list ap;
    int i, o = 0;
    
    m->n = size;
    m->msz = msz;
    m->left = 0;
    m->cur = 0;
    m->item = (menuitem_t *) malloc((size+1) * sizeof(menuitem_t));
    m->attr = C_MNU;

    va_start(ap, msz);
    for (i = 0; i < size; i++) {
        m->item[i].index = i;
        m->item[i].text = strdup(va_arg(ap, char *));
        m->item[i].hotkey = va_arg(ap, int);
        if (m->item[i].hotkey < 256 && isalpha(m->item[i].hotkey)) 
            m->item[i].hotkey = tolower(m->item[i].hotkey);
        m->item[i].hotpos = va_arg(ap, int);
        m->item[i].offset = o;
        o += strlen(m->item[i].text) + 2;
        m->item[i].callback = va_arg(ap, void *);
        m->item[i].checked = 0;
        m->item[i].checkpos = 1;
    }
    m->item[i].callback = va_arg(ap, void *);
    va_end(ap);
    m->item[i].index = i;
    m->item[i].offset = o; /* sentinel */
    mn_calc_size(m, NULL);
}

void mn_toggle(menuitem_t *m)
{
    if (m->checked > 0) m->checked ^= 2;
}

void mn_setchecked(menuitem_t *m, int expr)
{
    if (m->checked > 0) {
        if (expr) m->checked = 3;
        else m->checked = 1;
    }
}

int mn_ischecked(menuitem_t *m)
{
    return (m->checked == 3);
}

void mn_drawitem(menu_t *mn, int i, WINDOW *w)
{
    char *s;
    menuitem_t *m = &mn->item[i];
    
    s = m->text;
    if (m->checked > 0) {
        if (m->checked == 1) s[m->checkpos] = ' ';
        else s[m->checkpos] = 'x';
    }
    if (i == mn->cur) {
        if ((mn->attr & A_REVERSE) == A_REVERSE) 
            wattroff(w, A_REVERSE); else wattron(w, A_REVERSE);
    }
    waddstr(w, " ");
    if (m->hotpos >= 0) {
        waddnstr(w, s, m->hotpos);
        wattron(w, A_UNDERLINE);
        waddnstr(w, &s[m->hotpos], 1);
        wattroff(w, A_UNDERLINE);
        waddstr(w, &s[m->hotpos+1]);
    } else {
        waddstr(w, s);
    }
    waddstr(w, " ");
    if (i == mn->cur) {
        if ((mn->attr & A_REVERSE) == A_REVERSE) 
            wattron(w, A_REVERSE); else wattroff(w, A_REVERSE);
    }
}

void mn_draw(menu_t *m, int row, int col, WINDOW *w)
{
    int y, x, i, o;

    mn_calc_size(m, w);
    getyx(w, y, x);
    wmove(w, row, col);
    wattrset(w, m->attr);
    wprintw(w, "%*c", m->ms, ' ');
    wmove(w, row, col);
    o = m->item[m->left].offset;
    for (i = m->left; i < m->n; i++) {
        if (m->item[i+1].offset - o > m->ms) {
            wmove(w, row, col + m->ms - 1);
            waddstr(w, ">");
            break;
        }
        mn_drawitem(m, i, w);
        m->right = i;
    }
    wmove(w, row, col);
    if (m->left > 0) waddstr(w, "<");
    wmove(w, y, x);
    wattrset(w, C_NORMAL);
    wrefresh(w);
}


int submenu(int m)
{
    if (m == -1) return 1;
    else return m;
}

/* 
  return value:
    0  = key ignored
    1  = key handled by menu (i.e. arrows) or swallowed or submenu returned
    2  = menu item selected (i.e. enter)
    -1 = menu cancelled (i.e. escape)
    -2 = exit whole menu (all parents)

    on ENTER (etc.) the value of the callback (if any) is returned 
*/
int mn_keydown(menu_t *m, int key)
{
    switch (key) {
        case KEY_LEFT:
        case '[':
            if (m->cur > 0) {
                m->cur--;
                if (m->cur < m->left) m->left = m->cur;
            } else if (m->wrap) m->cur = m->n - 1;
            break;
        case KEY_PPAGE:
        case '<':
            m->cur = m->left;
            break;
        case KEY_NPAGE:
        case '>':
            m->cur = m->right;
            break;
        case KEY_HOME:
        case '{':
            m->cur = m->left = 0;
            break;
        case KEY_END:
        case '}':
            if (m->wrap) m->cur = m->n-1;
            break;
        case KEY_RIGHT:
        case ']':
            if (m->cur < m->n-1) {
                m->cur++;
                while (m->item[m->cur+1].offset - m->item[m->left].offset > m->ms && m->left < m->cur) m->left++;
            } else if (m->wrap) m->cur = 0;
            break;
        case 13:
        case 32:
             if (m->item[m->cur].callback != NULL) {
                 return submenu((*m->item[m->cur].callback)(&m->item[m->cur]));
             } else if (m->item[m->cur].checked > 0) {
                 mn_toggle(&m->item[m->cur]);
                 break;
             } else return 2;
        case 27:
        case '-':
        case KEY_BACKSPACE:
        /*case KEY_F(10):*/
        case 8: return -1;
        default: {
            int j;
            if (key <= 255 && isalpha(key)) key = tolower(key);
            for (j = 0; j < m->n; j++)
                if (m->item[j].hotkey >= 0 && m->item[j].hotkey == key) {
                    m->cur = j;
                    if (m->item[j].callback != NULL) 
                        return submenu((*m->item[j].callback)(&m->item[j]));
                    else return 2;
                }
            }
            if (m->item[m->n].callback == NULL) return 0;
            else return submenu((*m->item[m->n].callback)((void *) key));
    }
    return 1;
}
