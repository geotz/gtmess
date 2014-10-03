/*
 *    hash_tbl.c
 *
 *    hash table data structure
 *    Copyright (C) 2003-2004  George M. Tzoumas
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
#include"hash_tbl.h"

/* Chris Torek */
int hash_string(char *s)
{
    unsigned int v = 0;
    for(; *s; s++) {
        v *= 33;
        v += *s;
    }
    return v % HASH_LEN;
}

void hash_tbl_init(hash_table_t *tbl)
{
    int i;
    
    for (i = 0; i < HASH_LEN; i++) tbl->bucket[i] = NULL;
}

char *hash_tbl_find(hash_table_t *tbl, char *key)
{
    hash_entry_t *p;
    
    p = tbl->bucket[hash_string(key)];
    for (; p != NULL; p = p->next)
        if (strcmp(p->key, key) == 0) break;
    if (p != NULL) return p->value; else return NULL;
}

int hash_tbl_update(hash_table_t *tbl, char *key, char *value)
{
    hash_entry_t *p;
    int index;
    char *s;
    
    s = hash_tbl_find(tbl, key);
    if (s != NULL) {
        strcpy(s, value);
        return 0;
    } else {
        index = hash_string(key);
        p = (hash_entry_t *) malloc(sizeof(hash_entry_t));
        if (p == NULL) return -1;
        strcpy(p->key, key);
        strcpy(p->value, value);
        p->next = tbl->bucket[index];
        tbl->bucket[index] = p;
        return 1;
    }
}
