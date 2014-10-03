/*
 *    gtmess-gw.c
 *
 *    gtmess-gw - MSN Messenger HTTP Gateway
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

#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<pthread.h>
#include<string.h>

#include<sys/time.h>
#include<sys/types.h>
#include<netdb.h>
#include<sys/socket.h>

#include"../inty/inty.h"

#include"../config.h"


#define TM_POLL 3

#define SXL 8192
#define SML 512
#define SNL 96

typedef struct {
    char type[3]; /* NS or SB */
    char remote_ip[SML], gw_ip[SML];
    
    int opened, redirect, closed;
    char sess_maj[SNL], sess_min[SNL];
    int fd;
    buffer_t buf;
} msn_gw_t;

/* default addresses */
char msn_init_addr[SML] = "messenger.hotmail.com";
char msn_gateway_addr[SML] = "gateway.messenger.hotmail.com";
char msn_notif_addr[SML] = "207.46.104.20";

#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)

pthread_mutex_t sbhash_lock;

typedef struct sbentry_s {
    char hash[SNL], ip[SNL];
    struct sbentry_s *next;
} sbentry_t;

sbentry_t *sb_hlist;

void panic(char *s)
{
    fprintf(stderr, "FATAL ERROR: %s\n", s);
    exit(1);
}

void *Malloc(size_t size)
{
    void *p;
    
    if ((p = malloc(size)) == NULL) panic("malloc(): out of memory");
    return p;
}

sbentry_t *sbentry_alloc(char *hash, char *ip)
{
    sbentry_t *p = (sbentry_t *) Malloc(sizeof(sbentry_t));
    strcpy(p->hash, hash);
    strcpy(p->ip, ip);
/*    fprintf(stderr, "sbentry_alloc(%s, %s)\n", hash, ip);*/
    return p;
}

sbentry_t *sbentry_find(char *hash, sbentry_t *head, sbentry_t **prev)
{
    sbentry_t *p, *pr;
    
/*    fprintf(stderr, "sbentry_find(%s)\n", hash);*/
    for (pr = NULL, p = head; p != NULL; pr = p, p = p->next)
        if (strcmp(p->hash, hash) == 0) break;
    if (prev != NULL) *prev = pr;
    return p;
}

/* connect a client socket to a server and return a socket descriptor */
int Connect(char *hostname, int port)
{
    int sfd;
    struct sockaddr_in servaddr;
    struct hostent *host;
    
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    host = gethostbyname(hostname);
    if (host == NULL) return -2;
    
    servaddr.sin_addr.s_addr =  *((int *) host->h_addr_list[0]);
    
    if (connect(sfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
        return -3;
    
    return sfd;
}

int Read(int fd, void *buf, size_t count)
{
    int r;
    
    r = read(fd, buf, count);
    if (r < 0) perror("read()");
    return r;
}

int Write(int fd, void *buf, size_t count)
{
    int r;
    
    r = write(fd, buf, count);
    if (r < 0) perror("write()");
    return r;
}

void msn_gw_init(msn_gw_t *g, char *type, char *remote)
{
    g->type[0] = type[0];
    g->type[1] = type[1];
    g->type[2] = 0;
    
    g->opened = 0;
    g->redirect = 0;
    g->closed = 0;
    strcpy(g->remote_ip, remote);
    g->sess_maj[0] = g->sess_min[0] = 0;
    g->fd = -1;
}

int msn_gw_send(msn_gw_t *g, char *buf, int count)
{
    char s[SXL];
    int r;
    
    if (!g->opened) {
    /* need to open the server first */
        strcpy(g->gw_ip, msn_gateway_addr);
        g->fd = Connect(msn_init_addr, 80);
        if (g->fd < 0) {
            perror("ms_gw_send(): Connect()");
            return g->fd;
        }
        sprintf(s, "POST http://%s/gateway/gateway.dll?Action=open&Server=%s&IP=%s HTTP/1.1\r\n"
                   "Accept: */*\r\n"
                   "Accept-Language: en-us\r\n"
                   "Accept-Encoding: gzip, deflate\r\n"
                   "User-Agent: MSMSGS\r\n"
                   "Host: %s\r\n"
                   "Proxy-Connection: Keep-Alive\r\n"
                   "Pragma: no-cache\r\n"
                   "Content-Type: application/x-msn-messenger\r\n"
                   "Content-Length: %d\r\n\r\n",
                g->gw_ip, g->type, g->remote_ip, g->gw_ip, count);
        r = strlen(s);
        memcpy(s + r, buf, count);
        if (Write(g->fd, s, r + count) < 0) return -10;
        g->opened = 1;
    } else {
        if (g->redirect) {
            close(g->fd);
            g->fd = Connect(g->gw_ip, 80);
            if (g->fd < 0) {
                perror("ms_gw_send(redirect): ConnectToServer()");
                return g->fd;
            }
            g->redirect = 0;
        }
        sprintf(s, "POST http://%s/gateway/gateway.dll?SessionID=%s.%s HTTP/1.1\r\n"
                   "Accept: */*\r\n"
                   "Accept-Language: en-us\r\n"
                   "Accept-Encoding: gzip, deflate\r\n"
                   "User-Agent: MSMSGS\r\n"
                   "Host: %s\r\n"
                   "Proxy-Connection: Keep-Alive\r\n"
                   "Connection: Keep-Alive\r\n"
                   "Pragma: no-cache\r\n"
                   "Content-Type: application/x-msn-messenger\r\n"
                   "Content-Length: %d\r\n\r\n",
                g->gw_ip, g->sess_maj, g->sess_min, g->gw_ip, count);
        r = strlen(s);
        memcpy(s + r, buf, count);
        if (Write(g->fd, s, r + count) < 0) return -11;
    }
    return 0;
}

int msn_gw_poll(msn_gw_t *g)
{
    char s[SXL];
    
    if (!g->opened) return -2;
    sprintf(s, "POST http://%s/gateway/gateway.dll?Action=poll&SessionID=%s.%s HTTP/1.1\r\n"
               "Accept: */*\r\n"
               "Accept-Language: en-us\r\n"
               "Accept-Encoding: gzip, deflate\r\n"
               "User-Agent: MSMSGS\r\n"
               "Host: %s\r\n"
               "Proxy-Connection: Keep-Alive\r\n"
               "Connection: Keep-Alive\r\n"
               "Pragma: no-cache\r\n"
               "Content-Type: application/x-msn-messenger\r\n"
               "Content-Length: 0\r\n\r\n",
            g->gw_ip, g->sess_maj, g->sess_min, g->gw_ip);
            
    return Write(g->fd, s, strlen(s));
}

/* receive all content */
/* bug: header scanning doesn't stop at first \r\n\r\n */
int msn_gw_recv(msn_gw_t *g, char *buf, int limit)
{
    char s[SXL], tmp[SML];
    char *sp;
    int r, len, conlen, curlen, total;
    
    if (g->fd == -1) {
        fprintf(stderr, "msn_gw_recv(): gateway not initialized\n");
        return -1;
    }
    
    do {
        if ((len = Read(g->fd, s, SXL-1)) <= 0) return -1;
        s[len] = 0;
        r = 0; tmp[0] = 0;
        sscanf(s, "%*s %d %s", &r, tmp);
    } while (r == 100);
    
    if (r != 200) {
        fprintf(stderr, "msn_gw_recv(): unexpected gateway response: %d %s\n", r, tmp);
        return -2;
    }
    
    if ((sp = strstr(s, "GW-IP=")) != NULL) {
        sscanf(sp + 6, "%s", tmp);
        if (strcmp(g->gw_ip, tmp) != 0) {
            strcpy(g->gw_ip, tmp);
            g->redirect = 1;
        }
    }
    if ((sp = strstr(s, "SessionID=")) != NULL) {
        sscanf(sp + 10, "%[^.].%[^;]", g->sess_maj, g->sess_min);
    }
    if ((sp = strstr(s, "Session=close")) != NULL) {
        g->closed = 0;
        return 0;
    }
    
    conlen = curlen = total = 0;
    if ((sp = strstr(s, "Content-Length:")) != NULL)
        sscanf(sp + 15, "%d", &conlen);
    else {
        fprintf(stderr, "msn_gw_recv(): no content length\n");
        return -3;
    }
    
    if ((sp = strstr(s, "\r\n\r\n")) != NULL) {
        curlen = (&s[len] - sp) - 4;
        if (curlen > limit) {
            fprintf(stderr, "msn_gw_recv(): data will be truncated (needed %d/%d, avail. %d)\n",
                    curlen, conlen, limit);
            memcpy(buf, sp + 4, limit);
            return -4;
        } else memcpy(buf, sp + 4, curlen);
    } else {
        fprintf(stderr, "msn_gw_recv(): could not locate content\n");
        return -5;
    }
    
    total += curlen;
    limit -= curlen;
    conlen -= curlen;
    if (conlen > limit) {
        fprintf(stderr, "msn_gw_recv(): data will be truncated (needed %d, avail. %d)\n",
                conlen, limit);
        r = readx(g->fd, buf + curlen, limit);
        if (r != limit) perror("msn_gw_recv(): readx()");
        return -6;
    } else {
        r = readx(g->fd, buf + curlen, conlen);
        if (r != conlen) {
            perror("msn_gw_recv(): readx()");
            return -7;
        }
        total += r;
    }
    return total;
}

/*int  disp_port = 1863;*/
char notif_addr[SML] = "127.0.0.1";
int  notif_port = 1863;
char sboard_addr[SML] = "127.0.0.1";
int  sboard_port = 1864;

int is3(char *a, char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/* assuming that there won't be partial lines !!! */
void NotifStrProcess(char *dest, char *src, int size, int *dsize)
{
    char *s, *sp, *t;
    unsigned int tid;
    char arg1[SML], arg2[SML], 
          arg3[SML], arg4[SML], arg5[SML], arg6[SML];
    char tmp[SML];
    int tlen, darg;
    sbentry_t *p;
    
    *dsize = 0;
    s = src; t = dest;

    while ((sp = strstr(s, "XFR ")) != NULL || ((sp = strstr(s, "RNG ")) != NULL)) {
        tlen = sp - s;
        memcpy(t, s, tlen);
        *dsize += tlen;
        size -= tlen;
        t += tlen;
        if (is3(sp, "XFR"))
            if (sscanf(sp, "%*s %u %s %[^:]:%*d %s %s", 
                       &tid, arg1, arg2, arg3, arg4) == 5
                && strcmp(arg1, "SB") == 0) {
            
                p = sbentry_alloc(arg4, arg2);
                LOCK(&sbhash_lock);
                p->next = sb_hlist;
                sb_hlist = p;
                UNLOCK(&sbhash_lock);
                sprintf(tmp, "XFR %u %s %s:%d %s %s", 
                        tid, arg1, sboard_addr, sboard_port, arg3, arg4);
                tlen = strlen(tmp);
                memcpy(t, tmp, tlen);
                *dsize += tlen;
                t += tlen;
                s = strchr(sp, '\r');
                if (s == NULL) {
                    fprintf(stderr, "NotifStrProcess(): could not find '\\r'\n");
                    return;
                }
                size -= s - sp;
            } else if (sscanf(sp, "%*s %u %s %[^:]:%*d %d", 
                               &tid, arg1, arg2, &darg) == 4
                        && strcmp(arg1, "NS") == 0) {

                sprintf(tmp, "XFR %u %s %s:%d %d", 
                        tid, arg1, notif_addr, notif_port, darg);
                tlen = strlen(tmp);
                memcpy(t, tmp, tlen);
                *dsize += tlen;
                t += tlen;
                s = strchr(sp, '\r');
                if (s == NULL) {
                    fprintf(stderr, "NotifStrProcess(): could not find '\\r'");
                    return;
                }
                size -= s - sp;
            } else break;
        else if (is3(sp, "RNG")
                  && (sscanf(sp, "%*s %s %[^:]:%*d %s %s %s %s", 
                             arg1, arg2, arg3, arg4, arg5, arg6) == 6)) {

            p = sbentry_alloc(arg4, arg2);
            LOCK(&sbhash_lock);
            p->next = sb_hlist;
            sb_hlist = p;
            UNLOCK(&sbhash_lock);
            sprintf(tmp, "RNG %s %s:%d %s %s %s %s", 
                    arg1, sboard_addr, sboard_port, arg3, arg4, arg5, arg6);
            tlen = strlen(tmp);
            memcpy(t, tmp, tlen);
            *dsize += tlen;
            t += tlen;
            s = strchr(sp, '\r');
            if (s == NULL) {
                fprintf(stderr, "NotifStrProcess(): could not find '\\r'");
                return;
            }
            size -= s - sp;
        } else break;
    }
    
    while (size) {
        *t++ = *s++;
        size--;
        (*dsize)++;
    }
}

void *SBServer(void *arg)
{
    int fd = (int) arg;
    int r, max, sel;
    int waitreply;
    char s[SXL], hash[SXL];
    msn_gw_t g;
    fd_set rfds;
    struct timeval tv;
    sbentry_t *p, *prev;

    r = Read(fd, s, SXL-1);
    if (r <= 0) {
        close(fd);
        return NULL;
    }
    s[r] = 0;
    if (sscanf(s, "USR %*d %*s %s", hash) != 1
        && sscanf(s, "ANS %*d %*s %s", hash) != 1) {
        close(fd);
        return NULL;
    }
    
    LOCK(&sbhash_lock);
    p = sbentry_find(hash, sb_hlist, &prev);
    if (prev == NULL) sb_hlist = sb_hlist->next;
    else prev->next = p->next;
    UNLOCK(&sbhash_lock);
    
    if (p == NULL) {
        fprintf(stderr, "SBServer(): could not find switchboard server for the hash\n");
        close(fd);
        return NULL;
    }
    msn_gw_init(&g, "SB", p->ip);
    free(p);
    if (msn_gw_send(&g, s, r) < 0) {
        close(fd);
        return NULL;
    }
    waitreply = 1;
    while (1) {
        FD_ZERO(&rfds);
        max = 0;
        
        if (waitreply && g.opened) {
            FD_SET(g.fd, &rfds);
            max = g.fd;
        } else {
            FD_SET(fd, &rfds);
            max = fd;
        }
        
        tv.tv_sec = TM_POLL;
        tv.tv_usec = 0;
        sel = select(max + 1, &rfds, NULL, NULL, &tv);
        
        if (sel == -1) {
            perror("SBServer(): select()");
            break;
        } else if (sel == 0) {
            if (!waitreply) {
                msn_gw_poll(&g);
                waitreply = 1;
            }
        } else if (waitreply) {
            if ((r = msn_gw_recv(&g, s, SXL)) < 0) break;
            if (g.closed) break;
            if (r > 0 && Write(fd, s, r) < 0) break;
            waitreply = 0;
        } else {
            if ((r = Read(fd, s, SXL)) <= 0) break;
            if (msn_gw_send(&g, s, r) < 0) break;
            waitreply = 1;
        }
    }
    close(g.fd);
    close(fd);
    return NULL;
}

void *NotifServer(void *arg)
{
    int fd = (int) arg;
    int r, dlen, max, sel;
    int waitreply;
    char s[SXL], d[SXL];
    msn_gw_t g;
    fd_set rfds;
    struct timeval tv;

    msn_gw_init(&g, "NS", msn_notif_addr);
    waitreply = 0;
    while (1) {
        FD_ZERO(&rfds);
        max = 0;
        
        if (waitreply && g.opened) {
            FD_SET(g.fd, &rfds);
            max = g.fd;
        } else {
            FD_SET(fd, &rfds);
            max = fd;
        }
        
        tv.tv_sec = TM_POLL;
        tv.tv_usec = 0;
        sel = select(max + 1, &rfds, NULL, NULL, &tv);
        
        if (sel == -1) {
            perror("NotifServer(): select()");
            break;
        } else if (sel == 0) {
            if (!waitreply) {
                msn_gw_poll(&g);
                waitreply = 1;
            }
        } else if (waitreply) {
            if ((r = msn_gw_recv(&g, s, SXL-1)) < 0) break;
            if (g.closed) break;
            if (r > 0) {
                
                s[r] = 0;
                NotifStrProcess(d, s, r, &dlen);
                
                if (Write(fd, d, dlen) < 0) break;
            }
            waitreply = 0;
        } else {
            if ((r = Read(fd, s, SXL)) <= 0) break;
            if (msn_gw_send(&g, s, r) < 0) break;
            waitreply = 1;
        }
    }
    close(g.fd);
    close(fd);
    return NULL;
}

/*void *DispatchServer(void *arg)
{
    int fd = (int) arg;
    int r;
    unsigned int tid;
    char s[SML], com[SML];
    fd_set rfds;
    struct timeval tv;


    while (1) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 180;
        tv.tv_usec = 0;
        
        if (!select(fd+1, &rfds, NULL, NULL, &tv)) break;
        if ((r = Read(fd, s, SML-1)) <= 0) break;
        s[r] = 0;
        if (sscanf(s, "%s %u", com, &tid) != 2) break;
        if (is3(com, "VER")) {
            if (Write(fd, s, strlen(s)) < 0) break;
        } else if (is3(com, "INF")) {
            sprintf(s, "INF %u MD5\r\n", tid);
            if (Write(fd, s, strlen(s)) < 0) break;
        } else if (is3(com, "USR")) {
            sprintf(s, "XFR %u NS %s:%d 0 127.0.0.1:1863\r\n", tid, notif_addr, notif_port);
            Write(fd, s, strlen(s));
            break;
        } else break;
    }
    close(fd);
    return NULL;
}    */

int main()
{
/*    daemon_t disp;*/
    daemon_t notif, sb;
    
/*    disp.port = disp_port;
    disp.backlog = 10;
    disp.server_thread = DispatchServer;
    pthread_cond_init(&disp.cond, NULL);
    pthread_mutex_init(&disp.lock, NULL);
    disp.status = 0;*/
    
    notif.port = notif_port;
    notif.backlog = 10;
    notif.server_thread = NotifServer;
    pthread_cond_init(&notif.cond, NULL);
    pthread_mutex_init(&notif.lock, NULL);
    notif.status = 0;
    
    sb.port = sboard_port;
    sb.backlog = 10;
    sb.server_thread = SBServer;
    pthread_cond_init(&sb.cond, NULL);
    pthread_mutex_init(&sb.lock, NULL);
    sb.status = 0;
    
    pthread_mutex_init(&sbhash_lock, NULL);
/*    pthread_create(&disp.th_server, NULL, DaemonThread, (void *) &disp);*/
    pthread_create(&notif.th_server, NULL, DaemonThread, (void *) &notif);
    pthread_create(&sb.th_server, NULL, DaemonThread, (void *) &sb);
    
/*    pthread_join(disp.th_server, NULL);*/
    pthread_join(notif.th_server, NULL);
    pthread_join(sb.th_server, NULL);
    
    return 0;
}
