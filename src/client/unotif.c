/*
 *    unotif.c
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

#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<curses.h>

#include"unotif.h"
#include"gtmess.h"

int sound;  /* 0 = silence, 1 = beep, 2 = sound effects */
int popup;  /* popup window enabled/disabled */
int pfd;    /* pipe file descriptor */

char *snd_files[] = { NULL, NULL, "online.wav", "offline.wav", "newemail.wav",
        "type.wav", "ring.wav", "meout.wav" };

void unotif_init(int asound, int apopup)
{
    char tmp[256];
    
    sound = asound;
    popup = apopup;
    sprintf(tmp, "%s/notify.pip", Config.cfgdir);
    pfd = open(tmp, O_WRONLY | O_NONBLOCK);
}

void unotif_done()
{
    if (pfd >= 0) close(pfd);
}

void unotify(char *msg, snd_t effect)
{
    if (sound == 1) beep();
    if (pfd == -1) return;
    write(pfd, msg, strlen(msg));
}
