/*
 *    sound.c
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

#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<unistd.h>
#include<pthread.h>

#include"gtmess.h"
#include"sound.h"

pthread_cond_t cond_snd = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_snd = PTHREAD_MUTEX_INITIALIZER;

char *snd_files[] = { NULL, NULL, "online.wav", "offline.wav", "newemail.wav",
        "type.wav", "ring.wav", "meout.wav" };

snd_t sound_effect;

extern char **environ;
pthread_t thrid;

void exec_soundplayer(snd_t snd) {
    char soundfile[SCL];
    char strargs[SCL];
    
    sprintf(soundfile, "%s/%s", Config.snd_dir, snd_files[snd]);
    if (fork() == 0) {
        char *s;
        int ac = 1;
        char *args[64];
        strcpy(strargs, Config.snd_exec);
        args[0] = &strargs[0];
        if (Config.snd_redirect & 1 == 1) freopen("/dev/null", "w", stdout);
        if (Config.snd_redirect & 2 == 2) freopen("/dev/null", "w", stderr);
        for (s = &strargs[0]; *s && ac < 64; s++) { /* naive parsing */
            if (*s == ' ') {
                *s = 0;
                args[ac++] = s+1;
            } else if (*s == '%') {
                args[ac-1] = soundfile;
            }
        }
        args[ac] = NULL;
        execve(args[0], args, environ);
        exit(0);
    }
}

void *sound_daemon(void *dummy)
{
    pthread_detach(pthread_self());
    while (1) {
        LOCK(&lock_snd);
        pthread_cond_wait(&cond_snd, &lock_snd);
        while (waitpid(-1, NULL, WNOHANG) > 0); /* clean up zombies! */
        exec_soundplayer(sound_effect);
        UNLOCK(&lock_snd);
    }
}

void sound_init()
{
    pthread_create(&thrid, NULL, sound_daemon, NULL);
}
