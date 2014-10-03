/*
 *    msn.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2009  George M. Tzoumas
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iconv.h>

#include "../config.h"

#include "md5.h"
#include "msn.h"
#include "../inty/inty.h"
#include "util.h"

#include <assert.h>

char *msn_stat_name[] = {
    "Offline", "Appear Offline", "Online", "Idle",
    "Away", "Busy", "Be Right Back", "On the Phone",
    "Out to Lunch", "Unknown"};
char *msn_stat_com[] = { "FLN", "HDN", "NLN", "IDL",
    "AWY", "BSY", "BRB", "PHN", "LUN", "UNK"};
char msn_stat_char[10] = "  >-=*_#&?";

iconv_t msn_ic[2];

msn_group_t msn_group_0 = { "* individuals *", "", NULL };

/******* contact lists *******/

msn_contact_t *msn_clist_find(msn_clist_t *q, unsigned char lf, char *login)
{
    msn_contact_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if ((p->lflags & lf) == lf && strcmp(p->login, login) == 0) break;
    return p;
}

msn_contact_t *msn_clist_findu(msn_clist_t *q, unsigned char lf, char *uuid)
{
    msn_contact_t *p;
	
    for (p = q->head; p != NULL; p = p->next)
        if ((p->lflags & lf) == lf && strcmp(p->uuid, uuid) == 0) break;
    return p;
}

int msn_clist_count(msn_clist_t *q, unsigned char lf)
{
    msn_contact_t *p;
    int c = 0;

    for (p = q->head; p != NULL; p = p->next) if ((p->lflags & lf) == lf) {
        if (p->lflags & msn_FL) c += p->groups; else c++;
    }
    return c;
}

/* FL only *
int msn_clist_gcount(msn_clist_t *q, char *login)
{
    msn_contact_t *p;
    int c = 0;

    for (p = q->head; p != NULL; p = p->next)
        if (strcmp(p->login, login) == 0) c++;
    return c;
} */

/* FL only */
/* return val:
    -1 = contact not in list
     0 = contact in list, but up-to-date
     1 = contact updated
*/
int msn_clist_update(msn_clist_t *q, unsigned char lf, char *loginu,
                     char *nick, int status, int tm_last_char, int byuuid)
{
    msn_contact_t *p;
    int r = -1;
    char *s;

    for (p = q->head; p !=NULL; p = p->next) {
        if (byuuid) s = p->uuid; else s = p->login;
        if (((p->lflags & lf) == lf) && strcmp(s, loginu) == 0) {
            r = 0;
            if (nick != NULL && strcmp(p->nick, nick) != 0) {
                    p->dirty = 1;
                    Strcpy(p->nick, nick, SML);
                    r = 1;
            }
            if (status >= 0 && p->status != status) p->status = status, r = 1;
            if (tm_last_char >= 0) p->tm_last_char = tm_last_char, r = 1;
            break;
        }
    }
    return r;
}

int msn_clist_psm_update(msn_clist_t *q, char *login, char *psm)
{
    msn_contact_t *p;
    int r = -1;

    for (p = q->head; p !=NULL; p = p->next) {
        if (strcmp(p->login, login) == 0) {
            r = 0;
            if (psm != NULL) {
                if (strcmp(p->psm, psm) != 0) {
                    Strcpy(p->psm, psm, SML);
                    r = 1;
                }
            }
            break;
        }
    }
    return r;
}

void msn_contact_free(msn_contact_t *q)
{
    if (q == NULL) return;
    if (q->next != NULL) msn_contact_free(q->next);
    if (q->gid != NULL) {
        int i;
        assert(q->gidsize > 0);
        for (i = 0; i < q->groups; i++)	free(q->gid[i]);
        free(q->gid);
    }
    free(q);
}

void msn_clist_free(msn_clist_t *q)
{
    if (q->head != NULL) msn_contact_free(q->head);
    q->count = 0;
    q->head = NULL;
}

int msn_clist_rem_zombies(msn_clist_t *q)
{
    msn_contact_t *p;
    msn_contact_t *last, *next;
    int c = 0;

    p = q->head;
    last = NULL;
    while (1) {
        while (p != NULL && p->lflags != 0) {
            last = p;
            p = p->next;
        }
        if (p != NULL) {
            next = p->next;
            assert(p->groups == 0);
            /*if (p->gid != NULL) {
                int i;
                for (i = 0; i < p->groups; i++) free(p->gid[i]);
                free(p->gid);
            }*/
            if (p->gidsize) free(p->gid);
            free(p); c++;
            if (last != NULL) last->next = next; else q->head = next;
            p = next;
            q->count--;
        } else break;
    }
    return c;
}

int msn_clist_rem_grp(msn_clist_t *q, char *gid)
{
    msn_contact_t *p;
    int i, c = 0;

    for (p = q->head; p != NULL; p = p->next) {
        /* linear search, no problem since #groups small */
        for (i = 0; i < p->groups; i++) if (strcmp(p->gid[i], gid) == 0) {
			free(p->gid[i]);
            if (i < p->groups-1) memmove(&p->gid[i], &p->gid[i+1], (p->groups-i-1)*sizeof(char *));
            p->groups--;
            c++;
            break;
        }
    }
    return c;
}

void msn_clist_rem(msn_clist_t *q, unsigned char lf, char *loginu, char *gid)
{
    msn_contact_t *p;
    char *s;
    int i;

    for (p = q->head; p != NULL; p = p->next) {
        if (lf == msn_FL) s = p->uuid; else s = p->login;
        if (strcmp(s, loginu) != 0) continue;
        if ((p->lflags & lf) != lf) continue;
        
        if (lf == msn_FL) {
            /* linear search, no problem since #groups small */
            if (gid != NULL) {
                /* remove contact from given group ONLY */
                if (p->groups) {
                    i = 0;
                    while (i < p->groups)
                    if (strcmp(p->gid[i], gid) == 0) {
                        free(p->gid[i]);
                        if (i < p->groups-1) memmove(&p->gid[i], &p->gid[i+1], (p->groups-i-1)*sizeof(char *));
                        p->groups--;
                        break;
                    } else i++;
                }
            } else {
                /* remove contact from ALL groups and list */
                if (p->gid) {
                    for (i = 0; i < p->groups; i++) free(p->gid[i]);
                    free(p->gid);
                    p->gid = NULL;
                    p->groups = 0;
                    p->gidsize = 0;
                }
                p->lflags &= ~lf;
            }
        } else p->lflags &= ~lf;
        
        break; /* weird style, isn't it? :-) */
    }
    msn_clist_rem_zombies(q);
}

/* int msn_clist_load(msn_clist_t *q, FILE *f, int count)
{
    msn_contact_t t, *p, *cur, *last;
    int c = 0;
    
    while (count--) {
        memset(&t, 0, sizeof(t));
        if (fread(&t, sizeof(t), 1, f) != 1) return -1;
        p = (msn_contact_t *) malloc(sizeof(msn_contact_t));
        if (p == NULL) return -1;
        t.status = MS_FLN;
        /* sorted insertion *
        cur = q->head;
        last = NULL;
        memcpy(p, &t, sizeof(t));
        while (cur != NULL && p->gid > cur->gid) last = cur, cur = cur->next;
        p->next = cur;
        if (last != NULL) {
            last->next = p;
        } else q->head = p;

        q->count++;
        c++;
    }
    return c;
} */

/* int msn_clist_save(msn_clist_t *q, FILE *f)
{
    msn_contact_t *p;
    int c = 0;
    for (p = q->head; p != NULL; p = p->next) {
        if (fwrite(p, sizeof(msn_contact_t), 1, f) != 1) return -1;
        c++;
    }
    return c;
} */


/* gid == NULL: no gids will be stored (contact gid stays NULL) */
msn_contact_t *msn_clist_add(msn_clist_t *q, unsigned char lf, char *login, char *nick, char *gid, char *uuid)
{
    msn_contact_t *p;

    if ((p = msn_clist_find(q, 0, login)) != NULL) {
        if (gid != NULL && gid[0] && (lf & msn_FL)) {
            if (!msn_contact_belongs(p, gid)) {
                if (p->groups >= p->gidsize) {
                    if (p->gidsize == 0) p->gidsize = 4; else p->gidsize *= 2;
                    p->gid = (char **) realloc(p->gid, p->gidsize * sizeof(char *));
                }
                p->gid[p->groups++] = strdup(gid);
            }
        }
        p->lflags |= lf;
        return p;
    }

    p = (msn_contact_t *) calloc(1, sizeof(msn_contact_t));
    if (p == NULL) return NULL;
    p->lflags = lf;
    p->groups = 0;

    if (gid != NULL && gid[0]) {
        p->gidsize = 4;
        p->gid = (char **) malloc(p->gidsize * sizeof(char *));
        if (p->gid == NULL) {
            free(p);
            return NULL;
        }
        p->gid[p->groups++] = strdup(gid);
    } else {
        p->gid = NULL;
        p->gidsize = 0;
    }

    p->tm_last_char = 0;
    Strcpy(p->login, login, SML);
    if (nick != NULL && nick[0]) Strcpy(p->nick, nick, SML);
    else Strcpy(p->nick, p->login, SML);
    if (uuid != NULL && uuid[0]) Strcpy(p->uuid, uuid, SNL);
    else p->uuid[0] = 0;
    p->psm[0] = 0;
    p->dirty = 0;
    p->status = MS_FLN;
    p->notify = 1;
    p->ignored = 0;

    p->next = q->head;
    q->head = p;
    q->count++;
    return p;
}

/* if gid == NULL, then returns 1 when contact is GROUPLESS! */
int msn_contact_belongs(const msn_contact_t *q, char *gid)
{
    int i;
    if (gid == NULL) return q->groups == 0;
    for (i = 0; i < q->groups; i++)
        if (strcmp(q->gid[i], gid) == 0) return 1;
    return 0;
}

void msn_contact_cpy(msn_contact_t *dest, msn_contact_t *src)
{
    memcpy(dest, src, sizeof(msn_contact_t));
    if (dest->gid != NULL) {
        int i;
        dest->gid = (char **) malloc(dest->gidsize * sizeof(char *));
		for (i = 0; i < dest->groups; i++) dest->gid[i] = strdup(src->gid[i]);
    }
    dest->next = NULL;
}

void msn_clist_cpy(msn_clist_t *dest, msn_clist_t *src, unsigned char lf)
{
    msn_contact_t *p, *q;

    dest->count = 0;
    dest->head = NULL;
    q = dest->head;
    for (p = src->head; p != NULL; p = p->next) {
        if ((p->lflags & lf) != lf) continue;
        q = (msn_contact_t *) malloc(sizeof(msn_contact_t));
        if (q == NULL) return;
        msn_contact_cpy(q, p);
        q->next = dest->head;
        dest->head = q;
        dest->count++;
    }
}

/******* group lists *******/

msn_group_t *msn_glist_find(msn_glist_t *q, char *gid)
{
    msn_group_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if (strcmp(p->gid, gid) == 0) break;
    return p;
}

char *msn_glist_findn(msn_glist_t *q, char *gid)
{
    msn_group_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if (strcmp(p->gid, gid) == 0) return p->name;
    return "<unknown>";
}

void msn_group_free(msn_group_t *q)
{
    if (q->next != NULL) msn_group_free(q->next);
    free(q);
}

void msn_glist_free(msn_glist_t *q)
{
    if (q->head != NULL) msn_group_free(q->head);
    q->head = NULL;
    q->count = 0;
}

void msn_glist_ren(msn_glist_t *q, char *gid, char *newname)
{
    msn_group_t *p = q->head;

    while (p != NULL && strcmp(p->gid, gid) != 0) p = p->next;
    if (p != NULL) {
        Strcpy(p->name, newname, SML);
    }
}

void msn_glist_rem(msn_glist_t *q, char *gid)
{
    msn_group_t *p = q->head;
    msn_group_t *last = NULL, *next;

    while (p != NULL && strcmp(p->gid, gid) != 0) {
        last = p;
        p = p->next;
    }
    if (p != NULL) {
        next = p->next;
        free(p);
        if (last != NULL) last->next = next; else q->head = next;
        q->count--;
    }
}

/* int msn_glist_load(msn_glist_t *q, FILE *f, int count)
{
    msn_group_t t, *p, *cur, *last;
    int c = 0;
    
    while (count--) {
        memset(&t, 0, sizeof(t));
        if (fread(&t, sizeof(t), 1, f) != 1) return -1;
        p = (msn_group_t *) malloc(sizeof(msn_group_t));
        if (p == NULL) return -1;
        /* sorted insertion *
        cur = q->head;
        last = NULL;
        memcpy(p, &t, sizeof(t));
        while (cur != NULL && p->gid > cur->gid) last = cur, cur = cur->next;
        p->next = cur;
        if (last != NULL) {
            last->next = p;
        } else q->head = p;

        q->count++;
        c++;
    }
    return c;
}

int msn_glist_save(msn_glist_t *q, FILE *f)
{
    msn_group_t *p;
    int c = 0;
    for (p = q->head; p != NULL; p = p->next) {
        if (fwrite(p, sizeof(msn_group_t), 1, f) != 1) return -1;
        c++;
    }
    return c;
} */

msn_group_t *msn_glist_add(msn_glist_t *q, char *gid, char *name)
{
    msn_group_t *p, *cur, *last;

    p = (msn_group_t *) calloc(1, sizeof(msn_group_t));
    if (p == NULL) return NULL;
    Strcpy(p->gid, gid, SNL);
    Strcpy(p->name, name, SML);

    cur = q->head;
    last = NULL;
    /* insertion sort by name */
    while (cur != NULL && strcmp(p->name, cur->name) > 0) last = cur, cur = cur->next;
    p->next = cur;
    if (last != NULL) {
        last->next = p;
    } else q->head = p;
    q->count++;
    return p;
}

void msn_glist_cpy(msn_glist_t *dest, msn_glist_t *src, msn_contact_t *ref)
{
    msn_group_t *p, *q;

    dest->count = 0;
    dest->head = NULL;
    q = dest->head;
    for (p = src->head; p != NULL; p = p->next) {
        /* reference contact: copy only groups contact belongs to */
        if (ref != NULL) {
            int i;
            for (i = 0; i < ref->groups; i++)
                if (strcmp(ref->gid[i], p->gid) == 0) break;
            if (i == ref->groups) continue;
        }
        q = (msn_group_t *) malloc(sizeof(msn_group_t));
        if (q == NULL) return;
        memcpy(q, p, sizeof(msn_group_t));
        q->next = dest->head;
        dest->head = q;
        dest->count++;
    }
}

/******* string utility routines *******/

void md5hex(char *src, char *hex_output)
{
    md5_state_t state;
    md5_byte_t digest[16];
    int di;

    md5_init(&state);
    md5_append(&state, (const md5_byte_t *) src, strlen(src));
    md5_finish(&state, digest);
    for (di = 0; di < 16; ++di)
        sprintf(hex_output + di * 2, "%02x", digest[di]);
}

void utf8decode(iconv_t ic, char *src, char *dest)
{
    size_t ibl, obl;
    
    if (ic != (iconv_t) -1) {
        obl = ibl = strlen(src);
        while (ibl > 0) {
            iconv(ic, &src, &ibl, &dest, &obl);
            iconv(ic, NULL, NULL, NULL, NULL);
            if (ibl > 1) {
                *dest++ = '?'; src += 2;
                obl--; ibl -= 2;
            } else break;
        }
        *dest = 0;
    } else {
        while (*src) {
            *dest = *src++;
            if (*dest >= 0 && *dest < 32) *dest = '?';
            dest++;
        }
        *dest = 0;
    }
}

void utf8encode(iconv_t ic, char *src, char *dest, size_t obl)
{
    size_t ibl;
    
    if (ic != (iconv_t) -1) {
        ibl = strlen(src);
        while (ibl > 0 && obl > 0) {
            iconv(ic, &src, &ibl, &dest, &obl);
            iconv(ic, NULL, NULL, NULL, NULL);
            if (ibl > 0) {
                *dest++ = '?'; src++;
                obl--; ibl--;
            } else break;
        }
        if (obl > 0) *dest = 0;
    } else {
        while (*src) {
            *dest = *src++;
            if (*dest >= 0 && *dest < 32) *dest = '?';
            dest++;
        }
        *dest = 0;
    }
}

void str2url(char *src, char *dest)
{
    static char unreserved[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, /* !$'()*+,-. */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, /* 0123456789 */
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* ABCDEFGHIJKLMNOP */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, /* QRSTUVWXYZ_*/
      0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* abcdefghijklmnop */
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, /* qrstuvwxyz */
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    char textubuf[2*SXL], *textu;
    int alloc = 0;
    size_t size, len;

    len = strlen(src);
    if (msn_ic[1] != (iconv_t) -1) {
        if (6*len < 2*SXL) {
            textu = textubuf;
            alloc = 0;
            size = 2*SXL;
        } else {
            size = 6*len;
            textu = (char *) malloc(size+1);
            alloc = 1;
        }
        utf8encode(msn_ic[1], src, textu, size);
    } else {
        textu = src;
    }
    
    for (; *textu; textu++)
        if (unreserved[(unsigned char) *textu]) *dest++ = *textu;
        else sprintf(dest, "%%%02X", (unsigned char) *textu), dest += 3;
    *dest = 0;
    if (alloc && textu != NULL) free(textu);
}

void url2str(char *src, char *dest)
{
    unsigned int c;
    char *tmp, *t;
    
    t = tmp = strdup(src);
    while (*src != 0) {
        if (*src != '%') *t++ = *src++;
        else {
            sscanf(src, "%%%2x", &c);
            src += 3;
            *t++ = (unsigned char) c;
        }
    }
    *t = 0;
    utf8decode(msn_ic[0], tmp, dest);
    free(tmp);
}

int is3(char *a, char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}


/* buf contains server data, starting from "X=..." (just after tid)
   if login/nick/uuid are not NULL, then parsed data are stored there;
   if no data is found, then strings become EMPTY!
   returns a pointer to the first character after the last token parsed (usually ' ') */
char *msn_parse_contact_data(char *buf, char *login, char *nick, char *uuid)
{
    char *cur, t;
    char tmp[SML];

    if (login == NULL) login = &tmp[0];
    if (nick == NULL) nick = &tmp[0];
    if (uuid == NULL) uuid = &tmp[0];
    login[0] = nick[0] = uuid[0] = 0;
    cur = strchr(buf, '=');
    while (1) {
        char t;
        tmp[0] = 0;
        if (cur == NULL) return buf;
        t = *(cur-1);
        if (t == 'N') {
            sscanf(cur+1, "%s", login);
            cur += strlen(login) + 1; /* +1 for '=' */
        } else if (t == 'F') {
            sscanf(cur+1, "%s", tmp);
            cur += strlen(tmp) + 1;
            if (nick != &tmp[0]) url2str(tmp, nick);
        } else if (t == 'C') {
            sscanf(cur+1, "%s", uuid);
            cur += strlen(uuid) + 1;
        }
        buf = cur;
        cur = strchr(buf, '=');
    }
}

/******* errors *******/

char *msn_error_str(int err)
{
    switch (err) {
        case 200: return "syntax error";
        case 201: return "invalid parameter";
        case 205: return "invalid user";
        case 206: return "domain name missing";
        case 207: return "already logged in";
        case 208: return "invalid username";
        case 209: return "invalid nickname";
        case 210: return "user list full";
        case 215: return "user already on list";
        case 216: return "user not on list";
        case 217: return "user not online";
        case 218: return "already in mode";
        case 219: return "user is in the opposite list";
        case 223: return "too many groups";
        case 224: return "invalid group";
        case 225: return "user not in group";
        case 227: return "group not empty";
        case 229: return "group name too long";
        case 230: return "cannot remove group zero";
        case 231: return "invalid group";
        case 280: return "switchboard failed";
        case 281: return "transfer to switchboard failed";

        case 300: return "required field missing";
        case 301: return "too many hits to FND";
        case 302: return "not logged in";

        case 402:
        case 403: return "error accessing contact list";
        case 420: return "invalid account permissions";

        case 500: return "internal server error";
        case 501: return "database server error";
        case 502: return "command disabled";
        case 510: return "file operation failed";
        case 520: return "memory allocation error";
        case 540: return "challenge response failed";

        case 600: return "server is busy";
        case 601: return "server is unavailable";
        case 602: return "peer nameserver is down";
        case 603: return "database connection failed";
        case 604: return "server is going down";
        case 605: return "server unavailable";

        case 707: return "could not create connection";
        case 710: return "bad CVR parameters sent";
        case 711: return "write is blocking";
        case 712: return "session is overloaded";
        case 713: return "too many active users";
        case 714: return "too many sessions";
        case 715: return "not expected";
        case 717: return "bad friend file";
        case 731: return "not expected";

        case 800: return "nickname/status changes too rapidly";
        
        case 910: return "server too busy";
        case 911: return "authentication failed";
        case 912: return "server too busy";
        case 913: return "not allowed when offline";
        case 914: 
        case 915:
        case 916: return "server unavailable";
        case 917: return "authentication failed";
        case 918:
        case 919: return "server too busy";
        case 920: return "not accepting new users";
        case 921:
        case 922: return "server too busy";
        case 923: return "kids passport without parental consent";
        case 924: return "passport account not yet verified";
        case 928: return "bad ticket";
        case 931: return "account not on this server";

        default: return "<unknown server error>";
    }
}

/* io status */
char *msn_ios_str(int code)
{
    switch (code) {
        case  0: return "success";
        case -1: return "socket write error";
        case -2: return "socket read error";
        case -3: return "unexpected reply";
        default: return msn_error_str(code);
    }
}

/******* contact status *******/

msn_stat_t msn_stat_id(char *s)
{
    if (is3(s, msn_stat_com[MS_NLN])) return MS_NLN;
    if (is3(s, msn_stat_com[MS_FLN])) return MS_FLN;
    if (is3(s, msn_stat_com[MS_HDN])) return MS_HDN;
    if (is3(s, msn_stat_com[MS_IDL])) return MS_IDL;
    if (is3(s, msn_stat_com[MS_AWY])) return MS_AWY;
    if (is3(s, msn_stat_com[MS_BSY])) return MS_BSY;
    if (is3(s, msn_stat_com[MS_BRB])) return MS_BRB;
    if (is3(s, msn_stat_com[MS_PHN])) return MS_PHN;
    if (is3(s, msn_stat_com[MS_LUN])) return MS_LUN;
    
    return MS_UNK;
}

/******* protocol *******/

int writestr(int fd, char *s)
{
    return write(fd, s, strlen(s));
}

/* ping server */
int msn_png(int fd)
{
    return write(fd, "PNG\r\n", 5);
}

/* tell server we log out */
int msn_out(int fd)
{
    return write(fd, "OUT\r\n", 5);
}

int is_little_endian(void)
{
  int w = 1;
  return *((char *) &w);
}

unsigned int convert_endianess(unsigned int i)
{
    unsigned int r;
    r  = (i & 0x000000FF) << 0x18;
    r |= (i & 0x0000FF00) << 0x08;
    r |= (i & 0x00FF0000) >> 0x08;
    r |= (i & 0xFF000000) >> 0x18;
    return r;
}

/* adapted from original code by Tibor Billes */
int msn_chl(const char *challenge, const char *product_id, char *hash)
{
    char md5[36];
    unsigned int i[4], *j;
    int loop;
    size_t size;

    /* product_key must be equal to or greater than product_id */
    static const char *product_key = "YMM8C_H7KCQ2S_KL";
    char *chg;
    long long int low = 0, high = 0;
    int hash_[4], h1, h2, h3, h4;

    size = strlen(challenge) + strlen(product_key) + 8;
    chg = (char *) Malloc(size);
    if (chg == NULL) return -1;
    j = (unsigned int *) chg;

    strcpy(chg, challenge); strcat(chg, product_key);
    md5hex(chg, md5);

    sscanf(md5, "%8x%8x%8x%8x", &i[0], &i[1], &i[2], &i[3]);

    /* the string md5hex returned is big endian */
    if (is_little_endian()) {
        i[0] = convert_endianess(i[0]) & 0x7FFFFFFF;
        i[1] = convert_endianess(i[1]) & 0x7FFFFFFF;
        i[2] = convert_endianess(i[2]) & 0x7FFFFFFF;
        i[3] = convert_endianess(i[3]) & 0x7FFFFFFF;
    } else {
        i[0] &= 0x7FFFFFFF;
        i[1] &= 0x7FFFFFFF;
        i[2] &= 0x7FFFFFFF;
        i[3] &= 0x7FFFFFFF;
    }

    strcpy(chg, challenge); strcat(chg, product_id);
    for (loop = strlen(chg); loop % 8; loop++) chg[loop] = '0';
    chg[loop+1] = 0;

    if (!is_little_endian()) {
        for (loop = 0; loop < strlen(chg); loop += 4) {
            /* `/4' looks more beautiful and should get converted to >> 2
                by the compiler */
            j[loop/4] = convert_endianess(j[loop/4]);
        }
    }

    for (loop = 0; loop < strlen(chg)/4-1; loop += 2) {
        long long int temp = j[loop];
        temp = (temp * 0x0E79A9C1) % 0x7FFFFFFF;
        temp += high;
        temp = i[0] * temp + i[1];
        temp = temp % 0x7FFFFFFF;
        high = j[loop+1];
        high = (high + temp) % 0x7FFFFFFF;
        high = high * i[2] + i[3];
        high = high % 0x7FFFFFFF;

        low += high + temp;
    }
    high = (high + i[1]) % 0x7FFFFFFF;
    low = (low + i[3]) % 0x7FFFFFFF;

    sscanf(md5, "%8x%8x%8x%8x", &h1, &h2, &h3, &h4);

    if (is_little_endian()) {
        high = convert_endianess(high);
        low = convert_endianess(low);
        hash_[0] = h1^high;
        hash_[1] = h2^low;
        hash_[2] = h3^high;
        hash_[3] = h4^low;

    } else {
        hash_[0] = convert_endianess(h1)^high;
        hash_[1] = convert_endianess(h2)^low;
        hash_[2] = convert_endianess(h3)^high;
        hash_[3] = convert_endianess(h4)^low;
    }

    sprintf(hash, "%08x%08x%08x%08x", hash_[0], hash_[1], hash_[2], hash_[3]);
    free(chg);
    return 0;
}

/* reply to server challenge */
int msn_qry(int fd, unsigned int tid, const char *challenge)
{
    char reply[SNL], hash[33];
    static const char *product_id  = "PROD0090YUAUV{2B";

    msn_chl(challenge, product_id, hash);
    sprintf(reply, "QRY %u %s 32\r\n%s", tid, product_id, hash);
    return writestr(fd, reply);
}

/* change user status */
int msn_chg(int fd, unsigned int tid, msn_stat_t status)
{
    char s[SML];

    sprintf(s, "CHG %u %s\r\n", tid, msn_stat_com[status]);
    return writestr(fd, s);
}

/* set personal message */
int msn_uux(int fd, unsigned int tid, const char *psm)
{
    char reply[SML*3], s[SML*2], psmu[SML];
    str2url(psm, psmu);
    sprintf(s, " <Data><PSM>%s</PSM><CurrentMedia></CurrentMedia></Data>", psmu);
    sprintf(reply, "UUX %u %d\r\n%s", tid, strlen(s), s);
    return writestr(fd, reply);
}

/* sync contact lists */
int msn_syn(int fd, unsigned int tid, unsigned int ver)
{
    char s[SML];

    sprintf(s, "SYN %u %u 0\r\n", tid, ver);
    return writestr(fd, s);
}

/* change group name */
int msn_reg(int fd, unsigned int tid, char *gid, char *name)
{
    char s[SML], urln[SML];
    
    str2url(name, urln);
    sprintf(s, "REG %u %s %s\r\n", tid, gid, urln);
    return writestr(fd, s);
}

/* change contact name */
int msn_sbp(int fd, unsigned int tid, char *uuid, char *name)
{
    char s[SML], urln[SML];
   
    str2url(name, urln);
    sprintf(s, "SBP %u %s MFN %s\r\n", tid, uuid, urln);
    return writestr(fd, s);
}

/* change MY name */
int msn_prp(int fd, unsigned int tid, char *name)
{
    char s[SML], urln[SML];

    str2url(name, urln);
    sprintf(s, "PRP %u MFN %s\r\n", tid, urln);
    return writestr(fd, s);
}

/* remove from contact list */
int msn_rem(int fd, unsigned int tid, char list, char *loginu, char *gid)
{
    char s[SML];
    
    if (list == 'F') {
        if (gid != NULL) sprintf(s, "REM %u FL %s %s\r\n", tid, loginu, gid);
        else sprintf(s, "REM %u FL %s\r\n", tid, loginu);
    } else sprintf(s, "REM %u %cL %s\r\n", tid, list, loginu);
    return writestr(fd, s);
}

/* add to contact list */
int msn_adc(int fd, unsigned int tid, char list, char *loginu, char *gid)
{
    char s[SML];
    
    if (list == 'F') {
        if (gid == NULL || !gid[0]) sprintf(s, "ADC %u FL N=%s F=%s\r\n", tid, loginu, loginu);
        else sprintf(s, "ADC %u FL C=%s %s\r\n", tid, loginu, gid);
    } else sprintf(s, "ADC %u %cL N=%s\r\n", tid, list, loginu);
    return writestr(fd, s);
}

/* remove group */
int msn_rmg(int fd, unsigned int tid, char *gid)
{
    char s[SML];
    
    sprintf(s, "RMG %u %s\r\n", tid, gid);
    return writestr(fd, s);
}

/* add group */
int msn_adg(int fd, unsigned int tid, char *group)
{
    char s[SML], urln[SML];
    
    str2url(group, urln);
    sprintf(s, "ADG %u %s\r\n",tid, urln);
    return writestr(fd, s);
}

/* reverse list prompting: (A)lways/(N)ever */
int msn_gtc(int fd, unsigned int tid, char c)
{
    char s[SML];
    
    sprintf(s, "GTC %u %c\r\n", tid, c);
    return writestr(fd, s);
}

/* all other users: (B)locked/(A)llowed */
int msn_blp(int fd, unsigned int tid, char c)
{
    char s[SML];
    
    sprintf(s, "BLP %u %cL\r\n", tid, c);
    return writestr(fd, s);
}

int msn_cvr(int fd, unsigned int tid, char *cvr, char *login)
{
    char s[SML];
    if (login == NULL)
        sprintf(s, "CVR %u %s\r\n", tid, cvr);
    else
        sprintf(s, "CVR %u %s %s\r\n", tid, cvr, login);
    return writestr(fd, s);
}

/* starts the procedure to log in a notification/dispatch server 
in the case of a dispatch server, stores the new address into `dest'
in the case of a notification server it retrieves a hash into `dest';
the hash will be used for MD5 or TWN authentication;
cvr is used only for TWN authentication;

    uses 3 tids
    returns 
    0 on success, 
    1 in case of dispatch server redirecting us to a notification server */
int msn_login_init(int fd, unsigned int tid, char *login, char *cvr, char *dest)
{
    int err;
    char s[SXL], rep[SXL], tmp[SML];
    
    sprintf(s, "VER %u MSNP12 CVR0\r\n", tid++);
    if (writestr(fd, s) < 0) return -1;

    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "VER %*u %s", tmp) != 1 || 
        strcmp(tmp, "MSNP12") != 0) {
            close(fd);
            if (sscanf(rep, "%d", &err) == 1) return err;
            return -3;
    }

    if (msn_cvr(fd, tid++, cvr, login) < 0) return -1;
    
    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "%s", tmp) != 1 || strcmp(tmp, "CVR") != 0) {
        close(fd);
        if (sscanf(rep, "%d", &err) == 1) return err;
        return -3;
    }

    sprintf(s, "USR %u TWN I %s\r\n", tid++, login);
    if (writestr(fd, s) < 0) return -1;

    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "XFR %*u %*s %s", tmp) == 1) {
        if (dest != NULL) Strcpy(dest, tmp, SML);
        close(fd);
        return 1;
    }

    if (sscanf(rep, "USR %*u %*s S %s", dest) != 1) {
        if (sscanf(rep, "%d", &err) == 1) return err;
        return -3;
    }
    return 0;
}

/* finalizes the login procedure to a notification server using TWN authentication
    uses 1 tid
    returns 
    0 on success */
int msn_login_twn(int fd, unsigned int tid, char *ticket)
{
    int err;
    char s[SXL];
    int foo;
    
    sprintf(s, "USR %u TWN S %s\r\n", tid, ticket);
    if (writestr(fd, s) < 0) return -1;
    
    if (readlnt(fd, s, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(s, "USR %*u OK %*s %d", &foo) != 1) {
        if (sscanf(s, "%d", &err) == 1) return err;
        return -3;
    }
    
    return 0;
}

/* send typing notification */
int msn_msg_typenotif(int fd, unsigned int tid, char *user)
{
    char s[SXL], msg[SXL];

    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-msmsgscontrol\r\n"
                 "TypingUser: %s\r\n\r\n\r\n", user);
    sprintf(s, "MSG %u U %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* 
send a gtmess control message - among gtmess users
commands(args):
    BEEP = send a beep to sb (so that everybody pay attention to their console)
    GTMESS = tell everybody you are using gtmess, too
    SAY(text) = send <text> in a way that only gtmess users can see
*/
int msn_msg_gtmess(int fd, unsigned int tid, char *cmd, char *args)
{
    char s[2*SXL+6*48], msg[2*SXL+5*48], argsu[2*SXL];

    utf8encode(msn_ic[1], args, argsu, 2*SXL);
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-gtmesscontrol\r\n"
                 "Command: %s\r\n"
                 "Args: %s\r\n\r\n", cmd, argsu);
    sprintf(s, "MSG %u U %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* send text message */
int msn_msg_text(int fd, unsigned int tid, char *text)
{
    char s[SNL], textubuf[2*SXL], *textu;
    int alloc;
    size_t size, len;

    len = strlen(text);
    if (6*len < 2*SXL) {
        textu = textubuf;
        alloc = 0;
        size = 2*SXL;
    } else {
        size = 6*len;
        textu = (char *) malloc(size+1);
        alloc = 1;
    }
    utf8encode(msn_ic[1], text, textu, size);
    sprintf(s, "MSG %u N %d\r\n", tid, strlen(textu)+62);
    writestr(fd, s);
    writestr(fd, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/plain; charset=UTF-8\r\n\r\n");    /* len = 62 */
    writestr(fd, textu);
    if (alloc && textu != NULL) free(textu);
    return len;
}

/* send file invitation */
int msn_msg_finvite(int fd, unsigned int tid, unsigned int cookie, 
                    char *fname, unsigned int size)
{
    char s[SXL+9*48], msg[SXL+8*48], fnameu[SXL], *fc;
    
    if ((fc = strrchr(fname, '/')) != NULL) fname = fc + 1;
    utf8encode(msn_ic[1], fname, fnameu, SXL);
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-msmsgsinvite; charset=UTF-8\r\n\r\n"
                 "Application-Name: File Transfer\r\n"
                 "Application-GUID: {5D3E02AB-6190-11d3-BBBB-00C04F795683}\r\n"
                 "Invitation-Command: INVITE\r\n"
                 "Invitation-Cookie: %d\r\n"
                 "Application-File: %s\r\n"
                 "Application-FileSize: %u\r\n\r\n", cookie, fnameu, size);
    sprintf(s, "MSG %u N %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* send info for accepted (outgoing) file invitation */
int msn_msg_accept2(int fd, unsigned int tid, unsigned int cookie, 
                    char *ip, int port, unsigned int auth_cookie)
{
    char s[SXL], msg[SXL];
    
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-msmsgsinvite; charset=UTF-8\r\n\r\n"
                 "Invitation-Command: ACCEPT\r\n"
                 "Invitation-Cookie: %u\r\n"
                 "IP-Address: %s\r\n"
                 "Port: %d\r\n"
                 "AuthCookie: %u\r\n"
                 "Launch-Application: FALSE\r\n"
                 "Request-Data: IP-Address:\r\n\r\n", cookie, ip, port, auth_cookie);
    sprintf(s, "MSG %u N %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* accept (file) invitation */
int msn_msg_accept(int fd, unsigned int tid, unsigned int cookie)
{
    char s[SXL], msg[SXL];
    
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-msmsgsinvite; charset=UTF-8\r\n\r\n"
                 "Invitation-Command: ACCEPT\r\n"
                 "Invitation-Cookie: %u\r\n"
                 "Launch-Application: FALSE\r\n"
                 "Request-Data: IP-Address:\r\n\r\n", cookie);
    sprintf(s, "MSG %u N %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* cancel invitation */
int msn_msg_cancel(int fd, unsigned int tid, unsigned int cookie, char *code)
{
    char s[SXL], msg[SXL];
    
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/x-msmsgsinvite; charset=UTF-8\r\n\r\n"
                 "Invitation-Command: CANCEL\r\n"
                 "Invitation-Cookie: %u\r\n"
                 "Cancel-Code: %s\r\n\r\n", cookie, code);
    sprintf(s, "MSG %u N %d\r\n%s", tid, strlen(msg), msg);
    return writestr(fd, s);
}

/* call (invite) a user to a switchboard */
int msn_cal(int fd, unsigned int tid, char *user)
{
    char s[SML];

    sprintf(s, "CAL %u %s\r\n", tid, user);
    return writestr(fd, s);
}
