/*
 *    sound.h
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

#ifndef __SOUND_H__
#define __SOUND_H__

#include<pthread.h>

typedef enum {SND_NONE, SND_BEEP, SND_ONLINE, SND_OFFLINE, SND_NEWMAIL,
        SND_PENDING, SND_RING, SND_LOGOUT, SND_MSG } snd_t;

extern pthread_cond_t cond_snd;
extern pthread_mutex_t lock_snd;

extern snd_t sound_effect;

void sound_init();

#endif
