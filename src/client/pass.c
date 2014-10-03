/*
 *    pass.c
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

#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netinet/tcp.h>
#include<netdb.h>
#include<fcntl.h>
#include<signal.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<ctype.h>

#include"../config.h"

#include"gtmess.h"
#include"msn.h"

#include<openssl/ssl.h>
#include<openssl/err.h>

#include"../inty/inty.h"
#include"screen.h"


#define SBL 5120

SSL_CTX *ssl_ctx;

SSL_CTX *initialize_ctx()
{
    SSL_METHOD *meth;
    SSL_CTX *ctx;
    
    SSL_library_init();
    SSL_load_error_strings();


    signal(SIGPIPE, SIG_IGN);
    
    meth = SSLv23_method();
    ctx = SSL_CTX_new(meth);

    /* Load our keys and certificates*/
/*    SSL_CTX_use_certificate_chain_file(ctx, keyfile)))*/
    SSL_CTX_load_verify_locations(ctx, Config.ca_list, 0);
#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
    SSL_CTX_set_verify_depth(ctx, 1);
#endif
    
    return ctx;
}
     
int check_cert(SSL *ssl, char *host)
{
    X509 *peer;
    char peer_CN[256];
    int c;
    
    if(SSL_get_verify_result(ssl) != X509_V_OK) {
        if (Config.cert_prompt) { 
            msg(C_ERR, "SSL: certificate doesn't verify. Continue? ");
            msg2(C_MNU, "(Y)es  (N)o  (A)lways");
            while (1) {
                c = tolower(getch());
                if (c == 'n') {
                    msg(C_ERR, "No\n");
                    return -1;
                }
                if (c == 'y' || c == 'a') break;
                if (c == KEY_RESIZE) redraw_screen(1); else beep();
            }
            if (c == 'y') msg(C_ERR, "Yes\n");
            else Config.cert_prompt = 0, msg(C_ERR, "Always\n");
            msg2(C_MNU, copyright_str);
        } else msg(C_ERR, "SSL: certificate doesn't verify\n");
    }

    /*Check the common name*/
    peer = SSL_get_peer_certificate(ssl);
    X509_NAME_get_text_by_NID(X509_get_subject_name(peer),
                              NID_commonName, peer_CN, 256);
    if(strcmp(peer_CN, host)) {
        msg(C_ERR, "SSL: common name doesn't match host name\n"
                   "SSL: [%s]<=>[%s]\n", peer_CN, host);
        if (Config.common_name_prompt) {
            msg(C_ERR, "Continue? ");
            msg2(C_MNU, "(Y)es  (N)o  (A)lways");
            while (1) {
                c = tolower(getch());
                if (c == 'n') {
                    msg(C_ERR, "No\n");
                    return -1;
                }
                if (c == 'y' || c == 'a') break;
                if (c == KEY_RESIZE) redraw_screen(1); else beep();
            }
            if (c == 'y') msg(C_ERR, "Yes\n");
            else Config.common_name_prompt = 0, msg(C_ERR, "Always\n");
            msg2(C_MNU, copyright_str);
        }
    }
    return 0;
}


int write_ssl(SSL *ssl, char *s)
{
    int k, r;
    
    k = strlen(s);
    r = SSL_write(ssl, s, k);
    if (SSL_get_error(ssl, r) == SSL_ERROR_NONE) {      
        if(k != r) {
            /* incomplete write */
            return -1;
        }
    } else {
            /* SSL write problem */
        return -2;
    }
    return 0;
}

int read_ssl(SSL *ssl, char *dest, int count)
{
    int r, k, e;
    count--; k = 0;
    *dest = 0;
    while(count > 0) {
        r = SSL_read(ssl, dest+k, count);
        e = SSL_get_error(ssl, r);
        if (e == SSL_ERROR_NONE) {
            count -= r;
            k += r;
            dest[k] = 0;
        } else if (e == SSL_ERROR_ZERO_RETURN) return 1;
        else if (e == SSL_ERROR_SYSCALL) {
            /* SSL Error: Premature close */
            return -1;
        } else {
            /* SSL read problem */
            return -2;
        }
    }
    
    return 0;
    
}

char *CRLF = "\r\n";

int http_get(char *purl, char version, char*headers, char *response, int size, int *rcode)
{
    SSL *ssl;
    BIO *sbio;
    int fd;
    char *host, rsrc[SML], *s;
    int port;
    char request[SML], nurl[SXL], url[SXL];
    int code, redir = 2;
    
    code = 0;
    strcpy(url, purl);
    while (redir > 0) {
        host = strafter(url, "://");
        if (host == NULL) host = strdup(url); else host = strdup(host);
        s = strchr(host, '/');
        if (s == NULL) strcpy(rsrc, "/index.html"); else {
            strcpy(rsrc, s);
            *s = 0;
        }
        if (strncmp(url, "https:", 6) == 0) port = 443;
        else port = 80;
        sprintf(request, "GET %s HTTP/1.%c\r\n", rsrc, '0'+version);
        if (version == 1) {
            char *rhost = strdup(host);
            s = strchr(rhost, ':');
            if (s != NULL) *s = 0;
            strcat(request, "Host: ");
            strcat(request, rhost);
            strcat(request, CRLF);
            free(rhost);
        }
        if (headers != NULL) strcat(request, headers);
        strcat(request, CRLF);

        fd = ConnectToServer(host, port);
        if (fd < 0) {
            free(host);
            return fd;
        }        
        
        if (port == 443) {
            ssl = SSL_new(ssl_ctx);
            sbio = BIO_new_socket(fd, BIO_NOCLOSE);
            SSL_set_bio(ssl, sbio, sbio);
            if(SSL_connect(ssl) <= 0) {
                SSL_free(ssl);
                free(host);
                return -10;
            }
            if (check_cert(ssl, host) < 0) {
                SSL_free(ssl);
                free(host);
                return -13;
            }
            if (write_ssl(ssl, request) != 0) {
                SSL_free(ssl);
                free(host);
                return -11;
            }
            if (read_ssl(ssl, response, size) == -2) {
                SSL_free(ssl);
                free(host);
                return -12;
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
        } else {
            write(fd, request, strlen(request));
            memset(response, 0, size);
            readx(fd, response, size-1);
        }
        free(host);
        close(fd);
        
        sscanf(response, "%*s %d", &code);
        if (code < 300 || code >= 400) break;
        /* redirection */
        s = strafter(response, "Location: ");
        if (s == NULL) break;
        strcpy(nurl, s);
        s = strchr(nurl, '\r');
        if (s == NULL) break;
        *s = 0;
        /* redirect --> nurl */
        if (strcmp(url, nurl) == 0) break;
        strcpy(url, nurl);
        redir--;
    }
    if (rcode != NULL) *rcode = code;
    
    return 0;
}

char *get_login_server(char *dest)
{
    char buf[SBL], *s, *s2;
    int i, r;
    
    memset(buf, 0, sizeof(buf));
    if ((r = http_get("https://nexus.passport.com/rdr/pprdr.asp", 
                      0, NULL, buf, sizeof(buf), NULL)) != 0) {
        if (r == -10) msg(C_ERR, "SSL: connect error\n");
        else if (r == -11) msg(C_ERR, "SSL: write error\n");
        else if (r == -12) msg(C_ERR, "SSL: read error\n");
        else if (r != -13) msg(C_ERR, "ConnectToServer(): socket error\n");
        return NULL;
    }
    if ((s = strafter(buf, "\nPassportURLs:")) != NULL
        && (s = strafter(s, "DALogin=")) != NULL
        && (s2 = strchr(s, ',')) != NULL) {
        
        i = s2 - s;
        strncpy(dest, s, i);
        dest[i] = 0;
        return dest;
    }
    return NULL;
}

int get_ticket(char *server, char *login, char *pass, char *param, char *dest)
{
    char head[SBL], buf[SBL];
    char userver[SML], ulogin[SML], upass[SML];
    char *s, *s2;
    int code;
    
    str2url(login, ulogin);
    str2url(pass, upass);
    sprintf(userver, "https://%s", server);
    sprintf(head, "Authorization: Passport1.4 OrgVerb=GET,OrgURL=http%%3A%%2F%%2Fmessenger%%2Emsn%%2Ecom,"
                  "sign-in=%s,pwd=%s,%s\r\n", ulogin, upass, param);
    if (http_get(userver, 1, head, buf, sizeof(buf), &code) != 0) return -1;
    if (code / 100 == 4) return 400;
    if ((s = strafter(buf, "\nAuthentication-Info:")) == NULL) return -2;
    if (strstr(s, "da-status=success") == NULL) return -2;
    if ((s = strafter(s, "from-PP='")) == NULL) return -2;
    if ((s2 = strchr(s, '\'')) == NULL) return -2;
    *s2 = 0;
    strcpy(dest, s);
    return 0;
}

void pass_init()
{
    ssl_ctx = initialize_ctx();
}

void pass_done()
{
    SSL_CTX_free(ssl_ctx);
}
