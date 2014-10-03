/*
 *    inty.c
 *
 *    Inty Library - Simple library for network applications
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

#include<sys/select.h>
#include<sys/time.h>
#include<sys/types.h>
#include<netdb.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include"inty.h"

/* read exactly count bytes, waiting if necessary */
int readx(int fd, void *buf, int count)
{
    int k = 0, r;
    
    while (k < count) {
        r = read(fd, ((char *) buf)+k, count-k);
	if (r < 0) {
            if (k > 0) break; else return r;
        }
	if (r == 0) break;
	k += r;
    }
    return k;
}

/* read exactly count bytes, waiting if necessary, 
   but not more than timeout seconds for each packet */
int readxt(int fd, void *buf, int count, int timeout)
{
    int k = 0, r;
    fd_set rfds;
    struct timeval tv;
    
    while (k < count) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        if ((r = select(fd+1, &rfds, NULL, NULL, &tv)) == 1)
            r = read(fd, ((char *) buf)+k, count-k);
	if (r < 0) {
            if (k > 0) break; else return r;
        }
	if (r == 0) break;
	k += r;
    }
    return k;
}

/* read a line from <fd> into <buf> 
   at most <size>-1 bytes long and then store a \0 at the
   end (after the final \n), like fgets(), 
   returns NULL on failure or on not enough data 
   sorry, lots of context switching :-( */
char *readln(int fd, char *buf, int size)
{
    char *dest = buf;

    if (size <= 0) return NULL;
    size--;
    while (size > 0) {
        if (read(fd, (void *) dest, 1) <= 0) return NULL;
        size--;
        if (*dest++ == '\n') break;
    }
    *dest = 0;
    return (char *) buf;
}

/* like readln(), but waits at most timeout seconds for each byte */
char *readlnt(int fd, char *buf, int size, int timeout)
{
    char *dest = buf;
    fd_set rfds;
    struct timeval tv;
    
    if (size <= 0) return NULL;
    size--;
    while (size > 0) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        if (select(fd+1, &rfds, NULL, NULL, &tv) == 1) {
            if (read(fd, (void *) dest, 1) <= 0) return NULL;
        } else {
            return NULL;
        }
        size--;
        if (*dest++ == '\n') break;
    }
    *dest = 0;
    return (char *) buf;
}

/* hostname[:port] address parsing */
void ParseAddr(char *hostname, int *port, int def, char *addr)
{
    char *s, *t;
    
    *port = def;
    hostname[0] = 0;
    s = hostname; t = addr;
    while (*t != 0 && *t != ':') *s++ = *t++;
    *s = 0;
    if (*t == ':') sscanf(t+1, "%d", port);
}

/* connect a client socket to a server and return a socket descriptor */
int ConnectToServer(char *addr, int defport)
{
    int sfd;
    struct sockaddr_in servaddr;
    struct hostent *host;
    char hostname[256];
    int port;
    
    ParseAddr(hostname, &port, defport, addr);
    
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    host = gethostbyname(hostname);
    if (host == NULL)
        return -2;
    
    servaddr.sin_addr.s_addr =  *((int *) host->h_addr_list[0]);
    
    if (connect(sfd, (struct sockaddr *) &servaddr, sizeof(servaddr)))
        return -3;
    
    return sfd;
}

/* listen to port and handle client requests by forking and calling server_client() */
int ForkServer(int port, int backlog, void (*server_func)(int fd))
{
    int lsd, csd;
    struct sockaddr_in servaddr;
    
    if ((lsd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
/*        perror("ForkServer(): socket()");*/
        return -1;
    }
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    
    if (bind(lsd, (struct sockaddr *) &servaddr, sizeof(servaddr))) {
/*        perror("ForkServer(): bind()");*/
        return -2;
    }
    
    if (listen(lsd, backlog)) {
/*        perror("ForkServer(): listen()");*/
        return -3;
    }

    /* loop to fork servers */
    while (1) {    
	if ((csd = accept(lsd, (struct sockaddr *) NULL, NULL)) == -1) {
/*	    perror("ForkServer(): accept()");*/
            close(lsd);
            return -4;
	}	
	
	if (fork() == 0) {
	    /* server code */
/*            fprintf(stderr, "ForkServer(): accepted client request (%d)\n", csd);*/
	    close(lsd);
            
            (*server_func)(csd);
             
            close(csd);
/*            fprintf(stderr, "ForkServer(): connection with client closed (%d)\n", csd);*/
	    exit(0);
        } /* fork */
        
	close(csd);
    }
    
/*    return 0;*/
}    

/* listen to port and handle client requests by creating a thread that calls server_thread() */
void *DaemonThread(void *d_args)
{
    int lsd, csd;
    struct sockaddr_in servaddr;
    pthread_t th_lastclient;
    daemon_t *d = (daemon_t *) d_args;
    
    if ((lsd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        pthread_mutex_lock(&d->lock);
        d->status = -1;
        pthread_cond_signal(&d->cond);
        pthread_mutex_unlock(&d->lock);
        return (void *) -1;
    }
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(d->port);
    
    if (bind(lsd, (struct sockaddr *) &servaddr, sizeof(servaddr))) {
        pthread_mutex_lock(&d->lock);
        d->status = -2;
        pthread_cond_signal(&d->cond);
        pthread_mutex_unlock(&d->lock);
        return (void *) -2;
    }
    
    if (listen(lsd, d->backlog)) {
        pthread_mutex_lock(&d->lock);
        d->status = -3;
        pthread_cond_signal(&d->cond);
        pthread_mutex_unlock(&d->lock);
        return (void *) -3;
    }
    
    pthread_mutex_lock(&d->lock);
    d->status = 1;
    pthread_cond_signal(&d->cond);
    pthread_mutex_unlock(&d->lock);
    /* loop to serve clients */
    while (1) {    
	if ((csd = accept(lsd, (struct sockaddr *) NULL, NULL)) < 0) {
/*	    perror("DaemonThread(): accept()");*/
            close(lsd);
            return (void *) -4;
	}
        pthread_create(&th_lastclient, NULL, d->server_thread, (void *) csd);
        pthread_detach(th_lastclient);
    }
    
/*    return NULL;*/
}    

/* get local IP by looking at an open socket */
int GetMyIP(int fd, char *dest)
{
    struct sockaddr_in nam;
    int sz, r;

    sz = sizeof(nam);
    if ((r = getsockname(fd, (struct sockaddr *) &nam, &sz)) == 0) {
        sprintf(dest, "%s", inet_ntoa(nam.sin_addr));
    }
    return r;
}

char *strafter(const char *haystack, const char *needle)
{
    char *r;
    
    r = strstr(haystack, needle);
    if (r == NULL) return NULL;
    return r + strlen(needle);
}

void bfAssign(buffer_t *buf, int fd)
{
    buf->len = 0;
    buf->fd = fd;
}

int bfParseLine(buffer_t *buf, char *dest, int max)
{
    char *s = buf->data;
    char t;
    int k;
    
    max--;
    while (max > 0) {
        k = 0;
        while (buf->len > 0 && max > 0) {
            t = *dest++ = *s++;
            max--;
            buf->len--; k++;
            if (t == '\n') break;
        }
        if (k > 0 && t == '\n') {
            *dest = 0;
            memmove(buf->data, s, buf->len);
            return k;
        } else if (max > 0) {
            k = read(buf->fd, buf->data, BF_SIZE);
            if (k < 0) return k;
            buf->len = k;
            s = buf->data;
            if (k == 0) return 0;
        } else break;
    }
    return -2;
}

/* read exactly count bytes, waiting if necessary */
int bfReadX(buffer_t *buf, char *dest, int count)
{
    char *s = buf->data;
    int r, k = 0;
    
    if (count <= 0) return 0;
    while (1) {
        for (; k < count && buf->len > 0; k++, buf->len--) *dest++ = *s++;
        if (k == count) {
            if (buf->len > 0) memmove(buf->data, s, buf->len);
            return k;
        }
        r = read(buf->fd, buf->data, BF_SIZE);
        if (r < 0) return r;
        buf->len = r;
        if (r == 0) return k;
    }
}

int bfFlushN(buffer_t *buf, char *dest, int max)
{
    char *s = buf->data;
    int k = 0;
    
    for (; buf->len > 0 && k < max; buf->len--) {
        *dest++ = *s++;
        k++;
    }
    if (buf->len > 0) memmove(buf->data, s, buf->len);
    return k;
}
