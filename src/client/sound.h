/*
 *    sound.h
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

#ifndef __SOUND_H__
#define __SOUND_H__

#include<pthread.h>

enum {SND_NONE, SND_BEEP, SND_ONLINE, SND_OFFLINE, SND_NEWMAIL,
        SND_PENDING, SND_RING, SND_LOGOUT, SND_MSG };

typedef struct {
    double freq;
    int dur;
} songnote_t;

typedef struct {
    int size;
    songnote_t notes[8];
} song_t;

extern int snd_pfd[];

void sound_init();
void sound_done();

#endif
