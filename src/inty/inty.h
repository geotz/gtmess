/*
 *    inty.h
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

#ifndef _INTY_H_
#define _INTY_H_

#include<unistd.h>
#include<pthread.h>

#define SOCKET_TIMEOUT 180

/* read exactly count bytes, waiting if necessary */
int readx(int fd, void *buf, int count);

/* read exactly count bytes, waiting if necessary, 
   but not more than timeout seconds for each packet */
int readxt(int fd, void *buf, int count, int timeout);

/* read a line from <fd> into <buf> 
   at most <size>-1 bytes long and then store a \0 at the
   end (after the final \n), like fgets(), 
   returns NULL on failure or on not enough data */
char *readln(int fd, char *buf, int size);

/* like readln(), but waits at most timeout seconds for each byte */
char *readlnt(int fd, char *buf, int size, int timeout);

/* hostname[:port] address parsing */
void ParseAddr(char *hostname, int *port, int def, char *addr);

/* connect a client to a server and return a socket descriptor */
int ConnectToServer(char *addr, int defport);

/* listen to port and handle client requests by forking and calling server_func() */
int ForkServer(int port, int backlog, void (*server_func)(int fd));

typedef struct {
    int port;
    int backlog;
    void *(*server_thread)(void *);
    pthread_t th_server;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int status;
} daemon_t;

/* listen to port and handle client requests by creating a thread that calls server_thread() */
void *DaemonThread(void *d_args);

/* get local IP by looking at an open socket */
int GetMyIP(int fd, char *dest);

char *strafter(const char *haystack, const char *needle);

/* buffer routines */
#define BF_SIZE 4096

typedef struct {
    char data[BF_SIZE];
    int len;
    int fd;
} buffer_t;

void bfAssign(buffer_t *buf, int fd);
int bfParseLine(buffer_t *buf, char *dest, int max);
int bfReadX(buffer_t *buf, char *dest, int count);
int bfFlushN(buffer_t *buf, char *dest, int max);

#endif
