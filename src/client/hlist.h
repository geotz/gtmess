/*
 *    hlist.h
 *
 *    history list
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

#ifndef _HLIST_H_
#define _HLIST_H_

typedef struct hline_s {
    char *text;
    int attr;
    int len;
    struct hline_s *next, *prev;
} hline_t;

typedef struct {
    hline_t *start, *head;
    int count;
    int limit; /* -1 = infinite */
} hlist_t;

void hlist_init(hlist_t *h, int limit);
void hline_free(hline_t *l);
void hlist_free(hlist_t *h);
hline_t *hlist_find(hlist_t *h, char *s);
hline_t *hlist_add(hlist_t *h, char *s, int len, int attr);

#endif
