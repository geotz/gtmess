/*
 *    util.c
 *
 *    utility functions
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
#include <unistd.h>
#include <errno.h>

#include "util.h"
#include "screen.h"

void panic(char *s);

void *Malloc(size_t size)
{
    void *p;

    if ((p = malloc(size)) == NULL) panic("malloc(): out of memory");
    return p;
}

int Write(int fd, void *buf, size_t count)
{
    int r;

    r = write(fd, buf, count);
    if (r < 0) msg(C_ERR, "write(): %s\n", strerror(errno));
    return r;
}

void Strcpy(char *dest, const char *src, size_t n) 
{
    strncpy(dest, src, n);
    dest[n-1] = 0;
}

int valid_shell_string(char *s)
{
    char *t;
    t = strstr(s, "%");
    if (t == NULL) return 0;
    if (t[1] != 's') return 0;
    if (strstr(t + 2, "%") != NULL) return 0;
    return 1;
}

void cipher_string(unsigned char *s, unsigned char *key, int encode)
{
    static const int csize = 95; /* #32..#126 */
    int klen, len;
    klen = strlen((char *) key);
    len = strlen((char *) s);
    int i, j, m, k;
    if (klen == 0 || strlen((char *) s) == 0) return;
    i = j = 0;
    do {
        if ((s[i] < ' ') || (s[i] > '~')) j = 0;
        else {
            m = s[i] - 32;
            k = key[j % klen] - 32;
            j++;
            if (encode) m = (m + k) % csize; else m = (csize + m - k) % csize;
            s[i] = m + 32;
        }
        i++;
    } while (i < len);
}

