/*
 *    queue.h
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

#ifndef _QUEUE_H_
#define _QUEUE_H_

typedef struct qelem_s {
    void *data;
    int size;
    int type;
    
    struct qelem_s *next;
} qelem_t;

typedef struct {
    qelem_t *head;
    qelem_t *tail;
    int count;
} queue_t;

void queue_init(queue_t *q);
void queue_free(queue_t *q);
void qelem_free(qelem_t *e);

int queue_put(queue_t *q, int type, void *data, int size);
qelem_t *queue_get(queue_t *q);

#endif
