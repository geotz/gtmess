/*
 *    utf8.c
 *
 *    utf8 support routines
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

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "utf8.h"


int wstrwidth(const wchar_t *s, size_t n)
{
    int l  = 0;
    for (; *s != 0; s++) l += wcwidth_nl(*s);
    if (l <= n) return l; else return n;
}

int strlen_utf8(char *s, int *len)
{
    unsigned char *t = (unsigned char *) s;
    int k, i;
    
    k = i = 0;
    while (*t) {
        i++;
        if (((*t) & 0xc0) == 0x80) k++;
        t++;
    }
    if (len != NULL) *len = i;
    return i-k;
}

int strnlen_utf8(char *s, int n, int *len)
{
    unsigned char *t = (unsigned char *) s;
    int k, i, m = 0;
    
    k = i = 0;
    while (*t) {
        i++;
        if (((*t) & 0xc0) == 0xc0) m = 1;
        else if (((*t) & 0xc0) == 0x80) k++, m++;
        else m = 0;
        if (i > n) break;
        t++;
    }
    if (*t == 0) m  = 0;
    if (len != NULL) *len = i - m;
    return i-k-m;
}

/* len = size of buffer including \0 */
int stroffset(char *s, int n, int len)
{
    unsigned char *t = (unsigned char *) s;
    int c, i;
    
    c = i = 0;
    while (*t) {
        if (c == n) break;
        if (((*t) < 0x80) || ((*t) & 0xc0) == 0xc0) c++;
        i++;
        t++;
    }
    return (*t)? i-1: len - 1;
}

int seqlen(int c)
{
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    if ((c & 0xfc) == 0xf8) return 5;
    if ((c & 0xfe) == 0xfc) return 6;
    return 0;
}

int strwidth(char *s)
{
    mbstate_t mbs;
    wchar_t *w;
    const char *str = s;
    int len, width;
    
    if (s == NULL || *s == 0) return 0;
    len = strlen(s);
    w = (wchar_t *) malloc((len+1)*sizeof(wchar_t));
    memset(&mbs, 0, sizeof(mbs));
    mbsrtowcs(w, &str, len+1, &mbs);
    width = wstrwidth(w, wcslen(w));
    free(w);
    return width;
}

int widthoffset(char *s, int width)
{
    mbstate_t mbs;
    wchar_t *w;
    const char *str = s;
    int len, wlen, i;
    
    if (s == NULL) return -1;
    len = strlen(s);
    if (len == 0) return 0;
    w = (wchar_t *) malloc((len+1)*sizeof(wchar_t));
    memset(&mbs, 0, sizeof(mbs));
    mbsrtowcs(w, &str, len+1, &mbs);
    wlen = wcslen(w);
    for (i = 0; i < wlen; i++) {
        if (wstrwidth(w, i+1) > width) {
            free(w);
            return stroffset(s, i+1, len+1);
        }
    }
    free(w);
    return len;
}
