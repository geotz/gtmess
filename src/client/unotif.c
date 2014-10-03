/*
 *    unotif.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2007  George M. Tzoumas
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <curses.h>

#include "unotif.h"
#include "sound.h"
#include "gtmess.h"
#include "nserver.h"
#include "screen.h"

int pfd;    /* pipe file descriptor */

void unotif_init(int asound, int apopup)
{
    char tmp[256];
    
    if (asound == 2) sound_init();
    sprintf(tmp, "%s/notify.pip", Config.cfgdir);
    if (apopup) pfd = open(tmp, O_RDWR);
    else pfd = -1;
}

void unotif_done()
{
    if (pfd >= 0) close(pfd);
    sound_done();
}

void unotify(char *mesg, char effect)
{
    if ((Config.nonotif_mystatus & (1 << msn.status)) == (1 << msn.status)) return;
    playsound(effect);
    if (pfd == -1) return;
    write(pfd, mesg, strlen(mesg));
}

int can_notif(char *login) 
{
    msn_contact_t *p = msn_clist_find(&msn.CL, 0, login);
    if (p != NULL) return p->notify;
    else return 1;
}
