/*
 *    sound.c
 *
 *    gtmess - MSN Messenger client
 *    Copyright (C) 2002-2010  George M. Tzoumas
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "gtmess.h"
#include "sound.h"
#include "screen.h"

#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <fcntl.h>
#endif

int snd_pfd[2] = {-1, -1};

int pcspk_internal;

char *snd_files[] = { NULL, NULL, "online.wav", "offline.wav", "newemail.wav",
        "type.wav", "ring.wav", "meout.wav" };

song_t sng_online = { 2, { { 261.6, 100 }, { 392, 150 } } };
song_t sng_offline = { 4, { { 200, 20 }, { 1, 20 }, { 2000, 20 }, { 1800, 20 } } };
song_t sng_newemail = { 2, { { 1174.8, 100 }, { 987, 300 } } };
song_t sng_type = { 3, { { 392, 80 }, { 349.2, 80 }, { 329.6, 60 } } };
song_t sng_ring = { 3, { { 220, 50 }, { 1, 200 }, { 220, 50 } } };
song_t sng_meout = { 8, { { 659.2, 30 }, { 587.4, 30 }, { 523.2, 30 }, { 493.9, 30 }, 
                          { 440, 30 }, { 392, 30 }, { 349.2, 30 }, { 329.6, 120 } } };

song_t *songs[] = { NULL, NULL, &sng_online, &sng_offline, &sng_newemail, 
        &sng_type, &sng_ring, &sng_meout };

pthread_t thrid;

#ifdef __linux__
void pcspeaker_internal(double freq, int delay)
{
    int fd, fq = (1000.0 * freq);
    fd = open("/proc/self/fd/1", O_WRONLY|O_NONBLOCK);
    if ( fd != -1 ) {
        ioctl(fd, KDMKTONE, ((delay << 16) | (1193180000/fq)));
/*        usleep(delay*1000);*/
        close(fd);
    }; /* else msg(C_ERR, "pcspeaker(): could not open file descriptor\n"); */
}
#else
#define pcspeaker_internal(f,d) ;
#endif

void playsong_internal(song_t *song)
{
    int i;
    if (song == NULL) return;
    for (i = 0; i < song->size; i++) {
        pcspeaker_internal(song->notes[i].freq, song->notes[i].dur);
        if (i < song->size-1) usleep(song->notes[i].dur * 1000);
    }
}

void playsong_external(song_t *song)
{
    int i;
    char cmd[SML], tmp[SNL];
    if (song == NULL) return;
    
    sprintf(cmd, "beep -f %g -l %d", song->notes[0].freq, song->notes[1].dur);
    for (i = 1; i < song->size; i++) {
        sprintf(tmp, " -n -f %g -l %d", song->notes[i].freq, song->notes[i].dur);
        strcat (cmd, tmp);
    }
/*    strcat (cmd, " &");*/
    system(cmd);
}

void exec_soundplayer(char snd) {
    char soundfile[SCL];
    char cmd[SCL];
    
    if (Config.snd_exec[0]) {
        sprintf(soundfile, "%s/%s", Config.snd_dir, snd_files[snd]);
        sprintf(cmd, Config.snd_exec, soundfile);
        system(cmd);
    }
}

void playsound(char snd)
{
    if (Config.sound == 0) return;
    if (snd < SND_BEEP || snd > SND_LOGOUT) return;
    if (Config.sound == 1 || snd == SND_BEEP)
        beep();
    else if (Config.sound == 2)
        write(snd_pfd[1], &snd, 1);
    else if (Config.sound == 3)
        playsong_external(songs[snd]);
    else if (Config.sound == 4)
        playsong_internal(songs[snd]);
}

void *sound_daemon(void *dummy)
{
    char sound_effect;

/*    pthread_detach(pthread_self());*/
    
    while (read(snd_pfd[0], &sound_effect, 1) > 0)
        exec_soundplayer(sound_effect);

    close(snd_pfd[0]);
    
    return NULL;
}

void sound_init()
{
    if (pipe(snd_pfd) == 0)
        pthread_create(&thrid, NULL, sound_daemon, NULL);
    else msg(C_ERR, "sound_init(): could not intitialize sound daemon\n");
}

void sound_done()
{
    if (snd_pfd[0] != -1) {
        pthread_cancel(thrid);
        pthread_join(thrid, NULL);
    }
}
