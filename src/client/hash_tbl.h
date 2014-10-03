/*
 *    hash_tbl.h
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

#ifndef _HASH_TBL_H_
#define _HASH_TBL_H_

#define HASH_LEN 256
#define HDATASZ  512

typedef struct hash_entry_s {
    char key[HDATASZ], value[HDATASZ];
    struct hash_entry_s *next;
} hash_entry_t;

typedef struct {
    hash_entry_t *bucket[HASH_LEN];
} hash_table_t;

int hash_string(char *s);
void hash_tbl_init(hash_table_t *tbl);
char *hash_tbl_find(hash_table_t *tbl, char *key);
int hash_tbl_update(hash_table_t *tbl, char *key, char *value);

#endif
