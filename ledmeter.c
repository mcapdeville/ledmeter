/*
 *   ledmeter :  level meter ALSA plugin with gpio leds display
 *   base on ameter alsa plugin
 *   Copyright (c) 2015 by Marc Capdeville <m.capdeville@no-log.org>
 *   Copyright (c) 2005 by Laurent Georget <laugeo@free.fr>
 *   Copyright (c) 2001 by Abramo Bagnara <abramo@alsa-project.org>
 *   Copyright (c) 2002 by Steve Harris <steve@plugin.org.uk>
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as
 *   published by the Free Software Foundation; either version 2.1 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>

/*#include <curses.h>*/

#define SYS_CLASS_LEDS "/sys/class/leds"
#define LED_ON	"255"
#define LED_OFF "0"

typedef struct _Leds_S {
	int fd;
	int Threshold;
} Leds_t;


typedef struct _snd_pcm_scope_ledmeter {
  snd_pcm_t *pcm;
  snd_pcm_scope_t *s16;
  snd_pcm_uframes_t old;
  unsigned int nLeds;
  Leds_t *Leds;
  } snd_pcm_scope_ledmeter_t;


int LedMeter_FdBtoS16(float Thr) {

	return (((float)SHRT_MAX)*(powf(10,(Thr/20))));
}

static int level_enable(snd_pcm_scope_t * scope) {
	snd_pcm_scope_ledmeter_t *level = snd_pcm_scope_get_callback_private(scope);

	snd_pcm_scope_set_callback_private(scope, level);
	
	return (0);
}

static void level_disable(snd_pcm_scope_t * scope) {
	snd_pcm_scope_ledmeter_t *level =	snd_pcm_scope_get_callback_private(scope);
	(void)level;
 
}

static void level_close(snd_pcm_scope_t * scope)
{
	snd_pcm_scope_ledmeter_t *level = snd_pcm_scope_get_callback_private(scope);
	if (level)
		free(level); 
}

static void level_start(snd_pcm_scope_t * scope ATTRIBUTE_UNUSED) {
	sigset_t s;

	sigemptyset(&s);
	sigaddset(&s, SIGINT);
	pthread_sigmask(SIG_BLOCK, &s, NULL); 
}

static void level_stop(snd_pcm_scope_t * scope) {
	(void)scope;
}

static void level_update(snd_pcm_scope_t * scope) {
	snd_pcm_scope_ledmeter_t *level =	snd_pcm_scope_get_callback_private(scope);
	snd_pcm_t *pcm = level->pcm;
	snd_pcm_sframes_t size;
	snd_pcm_uframes_t size1, size2;
	snd_pcm_uframes_t offset, cont;
	unsigned int c, channels;
	int Lmoy=0;

	size = snd_pcm_meter_get_now(pcm) - level->old;

	if (size < 0)
		size += snd_pcm_meter_get_boundary(pcm);
	
	offset = level->old % snd_pcm_meter_get_bufsize(pcm);
	cont = snd_pcm_meter_get_bufsize(pcm) - offset;
	size1 = size;
	if (size1 > cont)
		size1 = cont;
	size2 = size - size1;

	channels = snd_pcm_meter_get_channels(pcm);
	for (c = 0; c < channels; c++) {
		int16_t *ptr;
		int s, lev = 0;
		snd_pcm_uframes_t n;

		ptr = snd_pcm_scope_s16_get_channel_buffer(level->s16,c) + offset;
		for (n = size1; n > 0; n--) {
			s = *ptr;
			if (s < 0)
				s = -s;
			if (s > lev)
				lev = s;
			ptr++;
		}

		ptr = snd_pcm_scope_s16_get_channel_buffer(level->s16, c);
		for (n = size2; n > 0; n--) {
			s = *ptr;
			if (s < 0)
				s = -s;
			if (s > lev)
				lev = s;
			ptr++;
		}

		Lmoy += lev;
	}

	Lmoy = (Lmoy + ((channels + 1)>>1))/channels;

	for (c=0;c<level->nLeds;c++) {
		if (Lmoy > level->Leds[c].Threshold)
			write(level->Leds[c].fd,LED_ON,sizeof(LED_ON));
		else
			write(level->Leds[c].fd,LED_OFF,sizeof(LED_OFF));
	}

	level->old = snd_pcm_meter_get_now(pcm);
}

static void level_reset(snd_pcm_scope_t * scope) {
	snd_pcm_scope_ledmeter_t *level = snd_pcm_scope_get_callback_private(scope);
	snd_pcm_t *pcm = level->pcm;
	
	level->old = snd_pcm_meter_get_now(pcm);
}

snd_pcm_scope_ops_t level_ops = {
  enable:level_enable,
  disable:level_disable,
  close:level_close,
  start:level_start,
  stop:level_stop,
  update:level_update,
  reset:level_reset,
};

int snd_pcm_scope_ledmeter_open(snd_pcm_t * pcm, const char *name,
			      snd_pcm_scope_t ** scopep) {
	snd_pcm_scope_t *scope, *s16;
	snd_pcm_scope_ledmeter_t *level;
	DIR * Led_dir=NULL;
	struct dirent *dirent;
	int ret,nLeds;
	char * Str,* Tok,Path[PATH_MAX];
	float Thr;
	Leds_t * Leds,*NewLeds;
	
	if (!(Led_dir = opendir(SYS_CLASS_LEDS))) {
		ret = errno;
		perror("opendir" SYS_CLASS_LEDS);
		return ret;
	}
	
	nLeds=0;
	Leds=NULL;
	while ((dirent = readdir(Led_dir))) {
		 
		Str = dirent->d_name;
		Tok = strstr(Str,"dB");
		if (!Tok)
			continue;
		if (strcmp(Tok,"dB"))
			continue;
		*Tok = '\0';

		ret= sscanf(Str,"%f",&Thr);
		if (ret!=1)
			continue;

		*Tok='d';

		NewLeds=realloc(Leds,(nLeds+1)*sizeof(Leds_t));
		if (!NewLeds) {
			ret = errno;
			if (Leds)
				free(Leds);
			closedir(Led_dir);
			return ret;
		}
		Leds = NewLeds;
		
		strcpy(Path,SYS_CLASS_LEDS "/");
		strncat(Path,dirent->d_name,sizeof(Path));
		strncat(Path,"/brightness",sizeof(Path));

		if (-1 == (Leds[nLeds].fd = open(Path,O_WRONLY))) {
			ret = errno;
			fprintf(stderr,"Openning led %s : ",Path);
			errno = ret;
			perror("");

			continue;
		}

		Leds[nLeds].Threshold = LedMeter_FdBtoS16(Thr);

		fprintf(stderr,"Found led %s with Trhresold = %d\n",dirent->d_name,Leds[nLeds].Threshold);
		
		nLeds++;
	}

	closedir(Led_dir);
	
	ret = snd_pcm_scope_malloc(&scope);
	if (ret<0)
	{
		if (Leds)
			free(Leds);
		return ret;
	}

	level = calloc(1, sizeof(*level));
	if (!level) {
		if (Leds)
			free(Leds);
		if (scope) free(scope);
		return -ENOMEM;
	}

	level->old=0;

	level->Leds = Leds;
	level->nLeds = nLeds;
	level->pcm = pcm;

	s16 = snd_pcm_meter_search_scope(pcm, "s16");
	if (!s16) {
		ret = snd_pcm_scope_s16_open(pcm, "s16", &s16);
		if (ret < 0) {
			if (Leds)
				free(Leds);
			if (scope)
				free(scope);
			if (level)
				free(level);
			return ret;
		}
	}

	level->s16 = s16;
	
	snd_pcm_scope_set_ops(scope, &level_ops);
	
	snd_pcm_scope_set_callback_private(scope, level);
	
	if (name)
		snd_pcm_scope_set_name(scope, strdup(name));

	snd_pcm_meter_add_scope(pcm, scope);
	
	*scopep = scope;
	
	return 0;
}

int _snd_pcm_scope_ledmeter_open(snd_pcm_t * pcm, const char *name,
			       snd_config_t * root, snd_config_t * conf) {
	snd_pcm_scope_t *scope;

	(void)root;
	(void)conf;
/*
	snd_config_iterator_t i, next;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		
		if (snd_config_get_id(n, &id) < 0)
			continue;
		
		if (strcmp(id, "comment") == 0)
			continue;
		
		if (strcmp(id, "type") == 0)
			continue;
		
		if (strcmp(id, "bar_width") == 0) {
			err = snd_config_get_integer(n, &bar_width);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
    		}

		if (strcmp(id, "decay_ms") == 0) {
			err = snd_config_get_integer(n, &decay_ms);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}

		if (strcmp(id, "peak_ms") == 0) {
			err = snd_config_get_integer(n, &peak_ms);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}

		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
*/
	return snd_pcm_scope_ledmeter_open(pcm, name, &scope);
}
