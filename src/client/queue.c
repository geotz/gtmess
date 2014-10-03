/*
 *    queue.c
 *
 *    queue data structure
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

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"queue.h"

void queue_init(xqueue_t *q)
{
    q->head = q->tail = NULL;
    q->count = 0;
}

void qelem_free(qelem_t *e)
{
    if (e->size > 0) free(e->data);
    free(e);
    e = NULL;
}

int queue_put(xqueue_t *q, int type, void *data, int size)
{
    qelem_t *e;
    
    e = (qelem_t *) malloc(sizeof(qelem_t));
    if (e == NULL) return -1;
    
    if (size > 0) {
        e->data = malloc(size);
        if (e->data == NULL) {
            free(e);
            return -2;
        }
        memcpy(e->data, data, size);
    } else e->data = data;
    
    e->size = size;
    e->type = type;
    
    e->next = NULL;
    if (q->count > 0) q->tail->next = e;
    else q->head = e;
    q->tail = e;
    
    q->count++;
    
    return 0;
}

qelem_t *queue_get(xqueue_t *q)
{
    qelem_t *e;
    
    e = q->head;
    if (q->count > 0) {
        if (q->count == 1) {
            q->head = q->tail = NULL;
        } else q->head = q->head->next;
        q->count--;
    }
    return e;
}
