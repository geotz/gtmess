/*
 *    unotif.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2005  George M. Tzoumas
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
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<curses.h>

#include"unotif.h"
#include"sound.h"
#include"gtmess.h"

int pfd;    /* pipe file descriptor */

void unotif_init(int asound, int apopup)
{
    char tmp[256];
    
    if (asound == 2) sound_init();
    sprintf(tmp, "%s/notify.pip", Config.cfgdir);
    if (apopup) pfd = open(tmp, O_WRONLY | O_NONBLOCK);
    else pfd = -1;
}

void unotif_done()
{
    if (pfd >= 0) close(pfd);
}


void playsound(snd_t snd)
{
    if (Config.sound == 0) return;
    if (snd < SND_BEEP || snd > SND_LOGOUT) return;
    if (Config.sound == 1 || snd == SND_BEEP) {
        beep();
        return;
    }
    LOCK(&lock_snd);
    sound_effect = snd;
    pthread_cond_signal(&cond_snd);
    UNLOCK(&lock_snd);
}

void unotify(char *msg, snd_t effect)
{
    playsound(effect);
    if (pfd == -1) return;
    write(pfd, msg, strlen(msg));
}
