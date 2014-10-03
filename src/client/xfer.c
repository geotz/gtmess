/*
 *    xfer.c
 *
 *    gtmess - MSN Messenger client
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

#include<sys/stat.h>
#include<sys/time.h>
#include<sys/types.h>
#include<unistd.h>
#include<stdlib.h>
#include<pthread.h>
#include<string.h>

#include"xfer.h"
#include"screen.h"
#include"gtmess.h"
#include"../inty/inty.h"

#define TM_DRAW 1

pthread_mutex_t XX = PTHREAD_MUTEX_INITIALIZER;
xfer_l XL;

extern char MyIP[SML];

char *xstat_str[] = { "???", "REQUEST", "ACCEPTED", 
        "CANCELLED", "REJECTED", "TIMEOUT", "FAILED",
        "UNABLE", "FTTIMEOUT", "OUT",
        "CONNECTED", "COMPLETE", "ABORTED" };

char incoming_dir[SML];

xfer_t *xfl_add(xfer_l *l, xclass_t xclass, xstat_t status, char *local, char *remote, 
                int incoming, unsigned int inv_cookie, msn_sboard_t *sb)
{
    xfer_t *x = malloc(sizeof(xfer_t));
    
    if (x == NULL) return NULL;
    
    x->xclass = xclass;
    x->status = status;
    strcpy(x->local, local);
    strcpy(x->remote, remote);
    x->incoming = incoming;
    x->inv_cookie = inv_cookie;
    x->sb = sb;
    pthread_mutex_init(&x->tlock, NULL);
    x->alive = 0;
    
    x->next = x->prev = NULL;
    
    if (l->count == 0) {
        l->head = l->tail = l->cur = x;
    } else {
        l->tail->next = x;
        x->prev = l->tail;
        l->tail = x;
    }
    x->index = l->count++;
    return x;
}

xfer_t *xfl_add_file(xfer_l *l, xstat_t status, char *local, char *remote, int incoming, 
                    unsigned int inv_cookie, msn_sboard_t *sb, 
                    char *fname, unsigned int fsize)
{
    xfer_t *x = xfl_add(l, XF_FILE, status, local, remote, incoming, inv_cookie, sb);
    if (x == NULL) return NULL;
    
    strcpy(x->data.file.fname, fname);
    x->data.file.size = fsize;
    x->data.file.sofar = 0;
    x->data.file.port = -1;
    x->data.file.auth_cookie = -1;
    x->data.file.ipaddr[0] = 0;
    
    return x;
}

xfer_t *xfl_find(xfer_l *l, unsigned int inv_cookie)
{
    xfer_t *x;
    
    for (x = l->head; x != NULL; x = x->next)
        if (x->inv_cookie == inv_cookie) break;
    return x;
}

xfer_t *xfl_findauth(xfer_l *l, unsigned int auth_cookie)
{
    xfer_t *x;
    
    for (x = l->head; x != NULL; x = x->next)
        if (x->xclass == XF_FILE && x->data.file.auth_cookie == auth_cookie) break;
    return x;
}

void xfl_rem(xfer_l *l, xfer_t *x)
{
    xfer_t *p;
    if (x == NULL || x->alive) return;
    pthread_mutex_destroy(&x->tlock);
    if (x->prev != NULL) x->prev->next = x->next;
    if (x->next != NULL) x->next->prev = x->prev;
    if (x == l->head) l->head = l->head->next;
    if (x == l->tail) l->tail = l->tail->prev;
    if (x == l->cur) {
        if (l->cur->next != NULL) l->cur = l->cur->next;
        else l->cur = l->cur->prev;
    }
    for (p = x->next; p != NULL; p = p->next) p->index--;
    l->count--;
    free(x);
}

void xf_print(xfer_t *x, char *dest)
{
    char cc;
    char *status, *inc, *s;
    char sgen[SML], sspec[SML], remote[SML];
    char ckilo[2] = {' ', 'k'};
    unsigned int kilo = 0; 
    
    if (x->xclass == XF_FILE) cc = 'F'; else cc = '?';
    if (x->incoming) inc = "FROM"; else inc = "TO";
    status = xstat_str[x->status];
    strcpy(remote, x->remote);
    if ((s = strstr(remote, "@hotmail.com")) != NULL) {
        s[1] = 0;
    }
    sprintf(sgen, "%c %s <%s> %s [%u] ", cc, inc, remote, status, x->inv_cookie);
    if (x->xclass == XF_FILE) {
        unsigned int percent;
        if (x->data.file.size == 0) percent = 100; 
        else percent = (unsigned int) (x->data.file.sofar*100.0/x->data.file.size);
        if (x->data.file.size >= 10240) kilo = 10;
        sprintf(sspec, "%s (%u/%u%c) %3u%%", x->data.file.fname, 
                x->data.file.sofar >> kilo, x->data.file.size >> kilo, ckilo[kilo != 0], percent);
    } else sspec[0] = 0;
    sprintf(dest, "%s | %s", sspec, sgen);
}

int top = 0;
int left = 0;

void xf_draw(TWindow *w)
{
    xfer_t *x;
    char tmp[SML] = {0};
            
    LOCK(&XX);
    LOCK(&w->lock);
    werase(w->wh);
    if (XL.cur != NULL) {
        if (XL.cur->index - top + 1> w->h) top = XL.cur->index - w->h + 1;
        else if (top > XL.cur->index) top = XL.cur->index;
    }
    wmove(w->wh, 0, 0);
    for (x = XL.head; x != NULL; x = x->next) {
        if (x->index < top) continue;
        wattrset(w->wh, (x->incoming)? attrs[C_DBG]: attrs[C_MSG]); 
        xf_print(x, tmp);
        if (XL.cur == x) {
            wbkgdset(w->wh, ' ' | attrs[C_MNU]);
            wclrtoeol(w->wh);
            waddch(w->wh, '>'); 
            wattrset(w->wh, attrs[C_MNU]);
        } else waddch(w->wh, ' ');
        if (left + w->w - 3 < SML) tmp[left + w->w - 3] = 0;
        wprintw(w->wh, " %s\n", tmp+left);
        wbkgdset(w->wh, ' ' | attrs[C_NORMAL]);
        if (x->index - top + 2 == w->h) break;
    }
    wmove(w->wh, w->h-1, 0);
    wbkgdset(w->wh, ' ' | attrs[C_MNU]);
    wclrtoeol(w->wh);
    wattrset(w->wh, attrs[C_MNU]);
    if (XL.count == 0) waddstr(w->wh, "list is empty");
    else wprintw(w->wh, "%d entr%s", XL.count, XL.count == 1? "y": "ies");
    wbkgdset(w->wh, ' ' | attrs[C_NORMAL]);
    UNLOCK(&w->lock);
    UNLOCK(&XX);
}

time_t tm_last_draw;

void draw_xfer(int r)
{
    xf_draw(&w_xfer);
    time(&tm_last_draw);
    if (wvis != 1) return;
    scr_event(SCR_XFER, NULL, 0, r);
}

void tm_draw_xfer(int r)
{
    time_t now;
    
    time(&now);
    if (now - tm_last_draw >= TM_DRAW) draw_xfer(r);
}


void xf_keydown(int c)
{
    xfer_t *x;
    LOCK(&XX);
/*    switch (c) {
        case '1': 
            xfl_add_file(&XL, XS_INVITE, "me@hotmail.com", "sue@hotmail.com", 1, 1234, NULL, "readme.txt", 10240);
            break;
        case '2':    
            xfl_add_file(&XL, XS_INVITE, "me@hotmail.com", SP, 0, 3433, NULL, "dd", 3576);
            break;
        case '3': 
            if (x != NULL) XL.cur->data.file.sofar++; 
            break;
        case '4':
            msg(C_ERR, "%s\n", MyIP);
            break;
    }*/
    x = XL.cur;
    if (x == NULL) {
        UNLOCK(&XX);
        return;
    }
    switch (c) {
        case 'a':
        /* accept incoming file */
            if (!x->incoming || x->xclass != XF_FILE) {
                beep();
                break;
            }
            if (x->status == XS_INVITE) {
                LOCK(&SX);
                msn_msg_accept(x->sb->sfd, x->sb->tid++, x->inv_cookie);
                UNLOCK(&SX);
            } else beep();
            break;
            
        case 'r':
        /* reject incoming file */
            if (!x->incoming || x->xclass != XF_FILE) {
                beep();
                break;
            }
            if (x->status == XS_INVITE) {
                LOCK(&SX);
                msn_msg_cancel(x->sb->sfd, x->sb->tid++, x->inv_cookie, "REJECT");
                UNLOCK(&SX);
                x->status = XS_REJECT;
            } else beep();
            break;
            
        case 'c':
        /* cancel (abort) incoming/outgoing file or outgoing invitation*/
            if (x->xclass != XF_FILE) {
                beep();
                break;
            }
            if (x->incoming == 0) {
                if (x->status == XS_INVITE) {
                    LOCK(&SX);
                    msn_msg_cancel(x->sb->sfd, x->sb->tid++, x->inv_cookie, "TIMEOUT");
                    UNLOCK(&SX);
                    x->status = XS_ABORT;
                } else if (x->status == XS_CONNECT) 
                            x->status = XS_ABORT, beep();
            } else {
                if (x->status == XS_CONNECT)
                    x->status = XS_ABORT;
                else beep();
            }
            break;
            
        case 'q':
        /* quick printout */
            msg(C_NORMAL, 
                "TRANSFER INFO:\nclass: %d, status: %s\nlocal: %s\n"
                "remote: %s\nincoming: %d, Invitation-Cookie: %u, "
                "thread alive: %d, index: %d\n", 
                x->xclass, xstat_str[x->status], x->local, x->remote,
                x->incoming, x->inv_cookie, x->alive, x->index);
            if (x->xclass == XF_FILE) {
                msg(C_NORMAL, 
                    "--file transfer details--\n"
                    "filename: %s\nsize: %u, bytes transfered: %u\n"
                    "IP-Address: %s, Port: %d, AuthCookie: %u\n",
                    x->data.file.fname, x->data.file.size,
                    x->data.file.sofar, x->data.file.ipaddr,
                    x->data.file.port, x->data.file.auth_cookie);
            }
            break;
        
        case '?':
        /* mini help */
            msg(C_NORMAL, "(A)ccept, (R)eject, (C)ancel\n");
            break;
            
        case ']':
        case KEY_DOWN:
        /* move down */
            if (XL.cur->next != NULL) {
                XL.cur = XL.cur->next;
                if (XL.cur->index - top + 2 > w_xfer.h) top++;
            }
            break;
            
        case '[':
        case KEY_UP:
        /* move up */
            if (XL.cur->prev != NULL) {
                XL.cur = XL.cur->prev;
                if (XL.cur->index < top) top--;
            }
            break;
            
        case '}':
        case KEY_RIGHT:
        /* scroll right */
            if (left < SML - w_xfer.w) left++;
            break;
            
        case '{':
        case KEY_LEFT:
        /* scroll left */
            if (left > 0) left--;
            break;
            
        case KEY_DC: {
        /* delete entry */
            if (x->alive) pthread_cancel(x->thrid);
            xfl_rem(&XL, x);
            if (XL.cur != NULL && XL.cur->index < top) top--;
            break;
        }
    }
    UNLOCK(&XX);
    draw_xfer(1);
}

char *fnuniq(char *dest, char *prefix, char *src)
{
    struct stat s;
    int k;
    char *e, *base, nul = 0;
    
    base = strdup(src);
    e = strchr(base, '.');
    if (e == NULL) e = &nul; else *e = 0, e = src + (e-base);
    if (prefix != NULL) {
        sprintf(dest, "%s/%s", prefix, src);
    } else strcpy(dest, src);
    for (k = 2; stat(dest, &s) == 0; k++) 
        if (prefix != NULL) sprintf(dest, "%s/%s_%d%s", prefix, base, k, e);
        else sprintf(dest, "%s_%d%s", base, k, e);
    free(base);
    if (prefix == NULL) return dest;
    else return dest+strlen(prefix)+1;
}

void msnftp_error(xfer_t *x, char *s)
{
    if (x->incoming) {
        msg(C_ERR, "MSNFTP:[%u] %s: %s\n", 
            x->inv_cookie, x->data.file.fname, s);
    } else {
        msg(C_ERR, "MSNFTPD:[%u] %s: %s\n", 
            x->inv_cookie, x->data.file.fname, s);
    }
}

void msnftp_cleanup(void *x_arg)
{
    xfer_t *x = (xfer_t *) x_arg;
    
    x->status = XS_ABORT;
    draw_xfer(1);
    LOCK(&x->tlock);
    x->alive = 0;
    UNLOCK(&x->tlock);  
}

void *msnftp_client(void *arg)
{
    xfer_t *x = (xfer_t *) arg;
    int fd;
    unsigned int len, k, rem;
    FILE *of;
    char ofname[SML], *ofnamep;
    char inp[SML], s[SML];
    unsigned char hdr[3], data[2048];
    
    LOCK(&x->tlock);
    x->thrid = pthread_self();
    x->alive = 1;
    UNLOCK(&x->tlock);
    pthread_cleanup_push(msnftp_cleanup, arg);
    fd = ConnectToServer(x->data.file.ipaddr, x->data.file.port);
    if (fd < 0) {
        msnftp_error(x, "could not connect to server");
        pthread_exit(NULL);
    }
    
    x->status = XS_CONNECT;
    draw_xfer(1);
    pthread_cleanup_push((void (*)(void *)) close, (void *) fd);
            
    strcpy(s, "VER MSNFTP\r\n");
    write(fd, s, 12);
    pthread_testcancel();
    inp[0] = 0;
    if (readlnt(fd, inp, SML, SOCKET_TIMEOUT) == NULL) msnftp_error(x, "remote cancel");
    pthread_testcancel();
    if (strcmp(s, inp) != 0) {
        msnftp_error(x, "protocol error (invalid version reply)");
        pthread_exit(NULL);
    }
    
    sprintf(s, "USR %s %u\r\n", x->local, x->data.file.auth_cookie);
    write(fd, s, strlen(s));
    pthread_testcancel();
    inp[0] = 0;
    if (readlnt(fd, inp, SML, SOCKET_TIMEOUT) == NULL) msnftp_error(x, "remote cancel");
    pthread_testcancel();
    if (sscanf(inp, "FIL %u", &len) != 1) {
        msnftp_error(x, "protocol error (no filesize)");
        pthread_exit(NULL);
    }
    if (len != x->data.file.size) {
        msnftp_error(x, "file size mismatch");
        pthread_exit(NULL);
    }
    
    write(fd, "TFR\r\n", 5);
    ofnamep = fnuniq(ofname, incoming_dir, x->data.file.fname);
    if (strcmp(ofnamep, x->data.file.fname) != 0) msnftp_error(x, ofname);
    of = fopen(ofname, "w");
    if (of == NULL) {
        write(fd, "CCL\r\n", 5);
        msnftp_error(x, "could not create local file");
        pthread_exit(NULL);
    }
    pthread_cleanup_push((void (*)(void *)) fclose, (void *) of);
    
    rem = len;
    while (rem) {
        if (x->status == XS_ABORT) break;
        hdr[0] = hdr[1] = hdr[2] = 0;
        pthread_testcancel();
        if (readxt(fd, hdr, 3, SOCKET_TIMEOUT) <= 0) {
            msnftp_error(x, "remote cancel");
            pthread_exit(NULL);
        }
        if (hdr[0] == 0) {
            len = (hdr[2] << 8) | hdr[1];
            pthread_testcancel();
            k = readxt(fd, data, len, SOCKET_TIMEOUT);
            if (k <= 0) {
                msnftp_error(x, "remote cancel");
                pthread_exit(NULL);
            }
            if (fwrite(data, len, 1, of) < 0) {
                write(fd, "CCL\r\n", 5);
                msnftp_error(x, "file write error");
                pthread_exit(NULL);
            }
            fflush(of);
            rem -= len;
            x->data.file.sofar += len;
            tm_draw_xfer(1);
        } else {
            msnftp_error(x, "remote cancel");
            pthread_exit(NULL);
        }
    }
    
    if (rem <= 0) {
        write(fd, "BYE 16777989\r\n", 14);
        msnftp_error(x, "finished");
        x->status = XS_COMPLETE;
    } else {
        write(fd, "CCL\r\n", 5);
        msnftp_error(x, "local cancel");
        x->status = XS_ABORT;
    }
    
    draw_xfer(1);
    pthread_cleanup_pop(1); /* fclose(of); */
    pthread_cleanup_pop(1); /* close(fd); */
    pthread_cleanup_pop(0); /* abort, alive = 0 */
    LOCK(&x->tlock);
    x->alive = 0;
    UNLOCK(&x->tlock);  
    return NULL;
}

void msnftpd_cleanup(void *arg)
{
    xfer_t *x = (xfer_t *) arg;
    
    x->status = XS_ABORT;
    draw_xfer(1);
    LOCK(&x->tlock);
    x->alive = 0;
    UNLOCK(&x->tlock);  
}

/*void msnftpd_debug(void *a)
{
    close((int) a);
    msg(C_ERR, "bye bye\n");
}*/

void *msnftp_server(void *arg)
{
    int fd = (int) arg;
    FILE *infp;
    fd_set rfds;
    struct timeval tv;
    char buf[SML], s[SML];
    unsigned int auth;
    xfer_t *x;
    unsigned int rem, len;
    char fbuf[2048];
    unsigned char *hdr;
    
    pthread_cleanup_push((void (*)(void *)) close, arg);
/*    pthread_cleanup_push(msnftpd_debug, arg);*/
    
/*msg(C_ERR, "new connection!\n");*/
    buf[0] = 0;
    if (readlnt(fd, buf, SML, SOCKET_TIMEOUT) == NULL) pthread_exit(NULL);
    if (strcmp("VER MSNFTP\r\n", buf) != 0) pthread_exit(NULL);
    write(fd, buf, 12);
    
    buf[0] = 0;
    if (readlnt(fd, buf, SML, SOCKET_TIMEOUT) == NULL) pthread_exit(NULL);
    if (sscanf(buf, "USR %*s %u", &auth) != 1) pthread_exit(NULL);
    LOCK(&XX);
    x = xfl_findauth(&XL, auth);
    UNLOCK(&XX);
    if (x == NULL) pthread_exit(NULL);
    LOCK(&x->tlock);
    if (!x->alive) {
        x->alive = 1;
        x->thrid = pthread_self();
    } else {
        UNLOCK(&x->tlock);
        pthread_exit(NULL);
    }
    UNLOCK(&x->tlock);
    pthread_cleanup_push(msnftpd_cleanup, (void *) x);
    
    x->status = XS_CONNECT;
    x->data.file.sofar = 0;
    draw_xfer(1);
    sprintf(s, "FIL %u\r\n", x->data.file.size);
    write(fd, s, strlen(s));
    pthread_testcancel();
    buf[0] = 0;
    if (readlnt(fd, buf, SML, SOCKET_TIMEOUT) == NULL) pthread_exit(NULL);
    pthread_testcancel();
    if (strcmp("TFR\r\n", buf) != 0) pthread_exit(NULL);
    infp = fopen(x->data.file.fname, "r");
    if (infp == NULL) {
        msnftp_error(x, "could not open file");
        pthread_exit(NULL);
    }
    
    rem = x->data.file.size;
    hdr = fbuf;
    while (rem > 0) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(fd+1, &rfds, NULL, NULL, &tv) == 1) {
            buf[0] = 0;
            if (readlnt(fd, buf, SML, SOCKET_TIMEOUT) != NULL && strcmp("CCL\r\n", buf) == 0) {
                msnftp_error(x, "remote cancel");
                pthread_exit(NULL);
            } else {
                msnftp_error(x, "protocol error (no CCL)");
                pthread_exit(NULL);
            }
        }
        if (x->status == XS_CONNECT) {
            hdr[0] = 0;
            if (rem >= 2045) {
                hdr[1] = 253; hdr[2] = 7;
                len = 2045; rem -= 2045;
            } else {
                hdr[1] = rem & 0xFF; 
                hdr[2] = rem >> 8;
                len = rem;
                rem = 0;
            }
        } else {
            hdr[0] = 1; hdr[1] = hdr[2] = 0;
            len = 0;
        }
        if (len > 0) fread(fbuf+3, len, 1, infp);
        if (write(fd, fbuf, len+3) < 0) {
            msnftp_error(x, "socket write error");
            pthread_exit(NULL);
        }
        x->data.file.sofar += len;
        tm_draw_xfer(1);
    }
    fclose(infp);
    draw_xfer(1);

    if (x->status == XS_CONNECT) {
        buf[0] = 0;
        if (readlnt(fd, buf, SML, SOCKET_TIMEOUT) == NULL) {
            msnftp_error(x, "protocol error (no BYE)");
        } else {
            if (strcmp(buf, "BYE 16777989\r\n") == 0)
                msnftp_error(x, "remote side has received the file");
            else
                msnftp_error(x, "protocol error (no BYE)"), msg(C_ERR, "%s\n", buf);
                
        }
        x->status = XS_COMPLETE;
        msnftp_error(x, "transfer finished");
    }
    draw_xfer(1);
    
    pthread_cleanup_pop(0); /* alive = 0 */
    LOCK(&x->tlock);
    x->alive = 0;
    UNLOCK(&x->tlock);  
    
    pthread_cleanup_pop(1); /* close(fd) */
    return NULL;
}

daemon_t msnftpd;

char *msnftpd_err_str[] = {"internal error", "socket()", "bind()", "listen()"};

void msnftp_init()
{
    if (Config.msnftpd == 1) {
        msnftpd.port = MSNFTP_PORT;
        msnftpd.backlog = 10;
        msnftpd.server_thread = msnftp_server;
        pthread_cond_init(&msnftpd.cond, NULL);
        pthread_mutex_init(&msnftpd.lock, NULL);
        msnftpd.status = 0;
    
        pthread_mutex_lock(&msnftpd.lock);
        pthread_create(&msnftpd.th_server, NULL, DaemonThread, (void *) &msnftpd);
        pthread_cond_wait(&msnftpd.cond, &msnftpd.lock);
        if (msnftpd.status == 1) pthread_detach(msnftpd.th_server);
        else {
            msg(C_ERR, "MSNFTPD (%d): %s error\n", msnftpd.status, msnftpd_err_str[-msnftpd.status]);
            Config.msnftpd = 0;
        }
    }
    sprintf(incoming_dir, "%s/received", Config.cfgdir);
}
