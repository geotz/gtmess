/*
 *    msn.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2004  George M. Tzoumas
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

#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<iconv.h>

#include"../config.h"

#include"md5.h"
#include"msn.h"
#include"../inty/inty.h"

char *msn_stat_name[] = {
    "Offline", "Appear Offline", "Online", "Idle",
    "Away", "Busy", "Be Right Back", "On the Phone",
    "Out to Lunch", "Unknown"};
char *msn_stat_com[] = { "FLN", "HDN", "NLN", "IDL",
    "AWY", "BSY", "BRB", "PHN", "LUN", "UNK"};
char msn_stat_char[10] = "  >-=*_#&?";

iconv_t msn_ic[2];
        
/******* contact lists *******/

msn_contact_t *msn_clist_find(msn_clist_t *q, char *login)
{
    msn_contact_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if (strcmp(p->login, login) == 0) break;
    return p;
}

int msn_clist_gcount(msn_clist_t *q, char *login)
{
    msn_contact_t *p;
    int c = 0;

    for (p = q->head; p != NULL; p = p->next)
        if (strcmp(p->login, login) == 0) c++;
    return c;
}

/* return val:
    -1 = contact not in list
     0 = contact in list, but up to date
     1 = contact updated
*/
int msn_clist_update(msn_clist_t *q, char *login,
                     char *nick, int status, int blocked, int tm_last_char)
{
    msn_contact_t *p;
    int r = 0, f = -1;

    for (p = q->head; p !=NULL; p = p->next)
        if (strcmp(p->login, login) == 0) {
            f++;
            if (nick != NULL) {
                if (strcmp(p->nick, nick) != 0) p->dirty = 1;
                strcpy(p->nick, nick);
                r = 1;
            }
            if (status >= 0) p->status = status, r = 1;
            if (blocked >= 0) p->blocked = blocked, r = 1;
            if (tm_last_char >= 0) p->tm_last_char = tm_last_char, r = 1;
        }
    if (f == -1) return -1;
    else return r;
}

void msn_contact_free(msn_contact_t *q)
{
    if (q->next != NULL) msn_contact_free(q->next);
    free(q);
}

void msn_clist_free(msn_clist_t *q)
{
    if (q->head != NULL) msn_contact_free(q->head);
    q->head = NULL;
    q->count = 0;
}

int msn_clist_rem_grp(msn_clist_t *q, msn_clist_t *rl, int gid)
{
    msn_contact_t *p, *rev;
    msn_contact_t *last, *next;
    int c = 0;

    p = q->head;
    last = NULL;
    while (1) {
        while (p != NULL && (p->gid != gid)) {
            last = p;
            p = p->next;
        }
        if (p != NULL) {
            next = p->next;
            if (rl != NULL && (rev = msn_clist_find(rl, p->login)) != NULL)
                rev->gid--;
            free(p); c++;
            if (last != NULL) last->next = next; else q->head = next;
            p = next;
            q->count--;
        } else break;
    }
    return c;
}

void msn_clist_rem(msn_clist_t *q, char *login, int gid)
{
    msn_contact_t *p = q->head;
    msn_contact_t *last = NULL, *next;

    while (p != NULL && (strcmp(p->login, login) != 0 || (gid >= 0 && p->gid != gid))) {
        /* de morgan ! */
        /* ! ((p == NULL) || (strcmp(p->login, login == 0) && (gid < 0 || p->gid == gid))) */
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

int msn_clist_load(msn_clist_t *q, FILE *f, int count)
{
    msn_contact_t t, *p, *cur, *last;
    int c = 0;
    
    while (count--) {
        memset(&t, 0, sizeof(t));
        if (fread(&t, sizeof(t), 1, f) != 1) return -1;
        p = (msn_contact_t *) malloc(sizeof(msn_contact_t));
        if (p == NULL) return -1;
        t.status = MS_FLN;
        /* sorted insertion */
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

int msn_clist_save(msn_clist_t *q, FILE *f)
{
    msn_contact_t *p;
    int c = 0;
    for (p = q->head; p != NULL; p = p->next) {
        if (fwrite(p, sizeof(msn_contact_t), 1, f) != 1) return -1;
        c++;
    }
    return c;
}

msn_contact_t *msn_clist_add(msn_clist_t *q, int gid, char *login, char *nick)
{
    msn_contact_t *p, *cur, *last;

    p = (msn_contact_t *) calloc(1, sizeof(msn_contact_t));
    if (p == NULL) return NULL;
    p->gid = gid;
    p->blocked = 0;
    p->tm_last_char = 0;
    strcpy(p->login, login);
    strcpy(p->nick, nick);
    p->dirty = 0;
    p->status = MS_FLN;

    /* sorted insertion */
    cur = q->head;
    last = NULL;
    while (cur != NULL && p->gid > cur->gid) last = cur, cur = cur->next;
    p->next = cur;
    if (last != NULL) {
        last->next = p;
    } else q->head = p;

/*    p->next = q->head;
    q->head = p;*/
    q->count++;
    return p;
}

void msn_clist_cpy(msn_clist_t *dest, msn_clist_t *src)
{
    msn_contact_t *p, *q;

    dest->count = 0;
    dest->head = NULL;
    q = dest->head;
    for (p = src->head; p != NULL; p = p->next) {
        q = (msn_contact_t *) malloc(sizeof(msn_contact_t));
        if (q == NULL) return;
        memcpy(q, p, sizeof(msn_contact_t));
        q->next = dest->head;
        dest->head = q;
        dest->count++;
    }
}

/******* group lists *******/

msn_group_t *msn_glist_find(msn_glist_t *q, int gid)
{
    msn_group_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if (p->gid == gid) break;
    return p;
}

char *msn_glist_findn(msn_glist_t *q, int gid)
{
    msn_group_t *p;

    for (p = q->head; p != NULL; p = p->next)
        if (p->gid == gid) return p->name;
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

void msn_glist_ren(msn_glist_t *q, int gid, char *newname)
{
    msn_group_t *p = q->head;

    while (p != NULL && p->gid != gid) p = p->next;
    if (p != NULL) {
        strcpy(p->name, newname);
    }
}

void msn_glist_rem(msn_glist_t *q, int gid)
{
    msn_group_t *p = q->head;
    msn_group_t *last = NULL, *next;

    while (p != NULL && p->gid != gid) {
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

int msn_glist_load(msn_glist_t *q, FILE *f, int count)
{
    msn_group_t t, *p, *cur, *last;
    int c = 0;
    
    while (count--) {
        memset(&t, 0, sizeof(t));
        if (fread(&t, sizeof(t), 1, f) != 1) return -1;
        p = (msn_group_t *) malloc(sizeof(msn_group_t));
        if (p == NULL) return -1;
        /* sorted insertion */
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
}

void msn_glist_add(msn_glist_t *q, int gid, char *name)
{
    msn_group_t *p, *cur, *last;

    p = (msn_group_t *) calloc(1, sizeof(msn_group_t));
    if (p == NULL) return;
    p->gid = gid;
    strcpy(p->name, name);

    cur = q->head;
    last = NULL;
    while (cur != NULL && p->gid > cur->gid) last = cur, cur = cur->next;
    p->next = cur;
    if (last != NULL) {
        last->next = p;
    } else q->head = p;
    q->count++;
}

void msn_glist_cpy(msn_glist_t *dest, msn_glist_t *src)
{
    msn_group_t *p, *q;

    dest->count = 0;
    dest->head = NULL;
    q = dest->head;
    for (p = src->head; p != NULL; p = p->next) {
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

    for (; *src; src++)
        if (unreserved[(unsigned char) *src]) *dest++ = *src;
        else sprintf(dest, "%%%02X", (unsigned char) *src), dest += 3;
    *dest = 0;
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

void utf8encode(iconv_t ic, char *src, char *dest, int obl)
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

/* reply to server challenge */
int msn_qry(int fd, unsigned int tid, char *hash)
{
    char reply[SNL], hex[SNL];

    strcat(hash, "Q1P7W2E4J9R8U3S5");
    md5hex(hash, hex);
    sprintf(reply, "QRY %u msmsgs@msnmsgr.com 32\r\n%s", tid, hex);
    return write(fd, reply, strlen(reply));
}

/* change user status */
int msn_chg(int fd, unsigned int tid, msn_stat_t status)
{
    char s[SNL];

    sprintf(s, "CHG %u %s\r\n", tid, msn_stat_com[status]);
    return write(fd, s, strlen(s));
}

/* sync contact lists */
int msn_syn(int fd, unsigned int tid, unsigned int ver)
{
    char s[SNL];

    sprintf(s, "SYN %u %u\r\n", tid, ver);
    return write(fd, s, strlen(s));
}

/* change group name */
int msn_reg(int fd, unsigned int tid, int gid, char *name)
{
    char s[SML], urln[SML];
    
    str2url(name, urln);
    sprintf(s, "REG %u %d %s 0\r\n", tid, gid, urln);
    return write(fd, s, strlen(s));
}

/* change name */
int msn_rea(int fd, unsigned int tid, char *login, char *name)
{
    char s[SML], urln[SML];
   
    str2url(name, urln);
    sprintf(s, "REA %u %s %s\r\n", tid, login, urln);
    return write(fd, s, strlen(s));
}

/* remove from contact list */
int msn_rem(int fd, unsigned int tid, char list, char *login, int gid)
{
    char s[SML];
    
    if (list == 'F') sprintf(s, "REM %u %cL %s %d\r\n", tid, list, login, gid);
    else sprintf(s, "REM %u %cL %s\r\n", tid, list, login);
    return write(fd, s, strlen(s));
}

/* add to contact list */
int msn_add(int fd, unsigned int tid, char list, char *login, int gid)
{
    char s[SML];
    
    if (list == 'F') sprintf(s, "ADD %u %cL %s %s %d\r\n", tid, list, login, login, gid);
    else sprintf(s, "ADD %u %cL %s %s\r\n", tid, list, login, login);
    return write(fd, s, strlen(s));
}

/* remove group */
int msn_rmg(int fd, unsigned int tid, int gid)
{
    char s[SML];
    
    sprintf(s, "RMG %u %d\r\n", tid, gid);
    return write(fd, s, strlen(s));
}

/* add group */
int msn_adg(int fd, unsigned int tid, char *group)
{
    char s[SML], urln[SML];
    
    str2url(group, urln);
    sprintf(s, "ADG %u %s 0\r\n",tid, urln);
    return write(fd, s, strlen(s));
}

/* reverse list prompting: (A)lways/(N)ever */
int msn_gtc(int fd, unsigned int tid, char c)
{
    char s[SML];
    
    sprintf(s, "GTC %u %c\r\n", tid, c);
    return write(fd, s, strlen(s));
}

/* all other users: (B)locked/(A)llowed */
int msn_blp(int fd, unsigned int tid, char c)
{
    char s[SML];
    
    sprintf(s, "BLP %u %cL\r\n", tid, c);
    return write(fd, s, strlen(s));
}

int msn_cvr(int fd, unsigned int tid, char *cvr, char *login)
{
    char s[SML];
    if (login == NULL)
        sprintf(s, "CVR %u %s\r\n", tid, cvr);
    else
        sprintf(s, "CVR %u %s %s\r\n", tid, cvr, login);
    return write(fd, s, strlen(s));
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
    
    sprintf(s, "VER %u MSNP9 CVR0\r\n", tid++);
    if (write(fd, s, strlen(s)) < 0) return -1;

    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "VER %*u %s", tmp) != 1 || 
        strcmp(tmp, "MSNP9") != 0) {
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
    if (write(fd, s, strlen(s)) < 0) return -1;

    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "XFR %*u %*s %s", tmp) == 1) {
        if (dest != NULL) strcpy(dest, tmp);
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
it retrieves the user's nickname into `nick'
    uses 1 tid
    returns 
    0 on success */
int msn_login_twn(int fd, unsigned int tid, char *ticket, char *nick)
{
    int err;
    char s[SXL], rep[SXL];
    
    sprintf(s, "USR %u TWN S %s\r\n", tid, ticket);
    if (write(fd, s, strlen(s)) < 0) return -1;
    
    if (readlnt(fd, rep, SXL, SOCKET_TIMEOUT) == NULL) return -2;
    if (sscanf(rep, "USR %*u OK %*s %s", s) != 1) {
        if (sscanf(rep, "%d", &err) == 1) return err;
        return -3;
    } else if (nick != NULL) url2str(s, nick);
    
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
    return write(fd, s, strlen(s));
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
    return write(fd, s, strlen(s));
}

/* send text message */
int msn_msg_text(int fd, unsigned int tid, char *text)
{
    char s[2*SXL+5*48], msg[2*SXL+4*48], textu[2*SXL];

    utf8encode(msn_ic[1], text, textu, 2*SXL);
    sprintf(msg, "MIME-Version: 1.0\r\n"
                 "Content-Type: text/plain; charset=UTF-8\r\n\r\n"
                 "%s", textu);
    sprintf(s, "MSG %u N %d\r\n%s", tid, strlen(msg), msg);
    return write(fd, s, strlen(s));
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
    return write(fd, s, strlen(s));
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
    return write(fd, s, strlen(s));
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
    return write(fd, s, strlen(s));
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
    return write(fd, s, strlen(s));
}

/* call (invite) a user to a switchboard */
int msn_cal(int fd, unsigned int tid, char *user)
{
    char s[SML];

    sprintf(s, "CAL %u %s\r\n", tid, user);
    return write(fd, s, strlen(s));
}
