/*
 *    hlist.c
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

#include<stdlib.h>

#include"hlist.h"

void hlist_init(hlist_t *h, int limit)
{
    h->head = h->start = NULL;
    h->count = 0;
    h->limit = limit;
}

void hline_free(hline_t *l)
{
    if (l->next != NULL) hline_free(l->next);
    free(l->text);
    free(l);
}

void hlist_free(hlist_t *h)
{
    if (h->start != NULL) {
        h->start->prev->next = NULL;
        hline_free(h->start);
    }
    h->start = NULL;
    h->count = 0;
}

hline_t *hlist_find(hlist_t *h, char *s)
{
    hline_t *l;
    
    if (h->start == NULL) return NULL;
    else if (strcmp(h->start->text, s) == 0) return h->start;
    else for (l = h->start->next; l != h->start; l = l->next)
        if (strcmp(l->text, s) == 0) return l;
    return NULL;
}

hline_t *hlist_add(hlist_t *h, char *s, int len, int attr)
{
    hline_t *l, *o;
    
    if (h->limit == 0) return NULL;
    
    if ((l = hlist_find(h, s)) != NULL) {
        if (l->prev == h->head) h->head = l;
        return l;
    }
    l = (hline_t *) malloc(sizeof(hline_t));
    if (l == NULL) return NULL;
    l->text = (char *) malloc(len+1);
    if (l->text == NULL) {
        free(l);
        return NULL;
    }
    
    l->len = len;
    l->attr = attr;
    strcpy(l->text, s);
    
    if (h->start == NULL) {
        /* empty list */
        h->head = l;
        l->next = l->prev = l;
        h->start = h->head;
    } else {
        /* not empty */
        if ((h->limit == -1) || (h->count < h->limit)) {
            /* limit not reached */
            /* insert right before start */
            h->start->prev->next = l;
            l->next = h->start;
            l->prev = h->start->prev;
            h->start->prev = l;
        } else {
            /* limit reached */
            /* drop first element */
            h->start->prev->next = l;
            l->next = h->start->next;
            l->prev = h->start->prev;
            h->start->next->prev = l;
            o = h->start;
            h->start = h->start->next;
            free(o->text);
            free(o);
            h->count--;
        }
    }
    
    h->count++;
    h->head = h->start->prev;
    return l;
}
