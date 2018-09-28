/*
 *  PCM - USB_STREAM plugin
 *
 *  Copyright (c) 2008, 2010 by Karsten Wiese <fzu@wemgehoertderstaat.de>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define _GNU_SOURCE
#include <byteswap.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h> 

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/hwdep.h>

#include "usb_stream.h"

#define DEBUG
#ifdef DEBUG
#define DBG(f, ...)	\
	fprintf(stderr, "%s:%i %i "f"\n", __FUNCTION__, __LINE__, getpid(), ## __VA_ARGS__);
#else
#define DBG(f, ...)
#endif

#ifdef VDEBUG
#define VDBG(f, ...)	\
	fprintf(stderr, "%s:%i %i "f"\n", __FUNCTION__, __LINE__, getpid(), ## __VA_ARGS__);
#else
#define VDBG(f, ...)
#endif

#define FRAME_SIZE 6

#define LCARD 32
struct user_usb_stream {
	char			card[LCARD];
	unsigned		use;
	struct usb_stream	*s;
	void			*write_area;
	struct user_usb_stream	*next;
};

typedef struct {
	snd_pcm_ioplug_t	io;

	snd_hwdep_t		*hwdep;
	struct user_usb_stream	*uus;

	struct pollfd		pfd;

	unsigned int		num_ports;
	unsigned		periods_start;
	unsigned		periods_done;

	unsigned 		channels;
	snd_pcm_uframes_t	period_size;
	unsigned int		rate;
} snd_pcm_us_t;

static struct user_usb_stream *uus;
static pthread_mutex_t uus_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct user_usb_stream *get_uus(const char *card)
{
	pthread_mutex_lock(&uus_mutex);

	struct user_usb_stream **l_uus = &uus,
				*r_uus = NULL;
	while (*l_uus) {
		if (strcmp((*l_uus)->card, card) == 0) {
			r_uus = *l_uus;
			r_uus->use++;
			goto unlock;
		}
		l_uus = &(*l_uus)->next;
	}
	r_uus = calloc(1, sizeof(*r_uus));
	if (r_uus) {
		r_uus->use = 1;
		strcpy(r_uus->card, card);
		*l_uus = r_uus;
	}

unlock:
	pthread_mutex_unlock(&uus_mutex);
	return r_uus;
}

static void uus_free(snd_pcm_us_t *us)
{
	if (!us->uus)
		return;

	pthread_mutex_lock(&uus_mutex);
	us->uus->use--;
	if (!us->uus->use) {
		struct user_usb_stream	**n_uus = &uus,
					*p_uus;
		while (us->uus != *n_uus) {
			p_uus = *n_uus;
			n_uus = &p_uus->next;
		}
		*n_uus = us->uus->next;
		if (us->uus->s) {
			munmap(us->uus->write_area, us->uus->s->write_size);
			munmap(us->uus->s, us->uus->s->read_size);
		}
		free(us->uus);
	}
	pthread_mutex_unlock(&uus_mutex);
}

static void us_free(snd_pcm_us_t *us)
{
	uus_free(us);
	free(us);
}

static int snd_pcm_us_close(snd_pcm_ioplug_t *io)
{
	snd_pcm_us_t *us = io->private_data;
	snd_hwdep_close(us->hwdep);
	us_free(us);
	return 0;
}

static snd_pcm_sframes_t snd_pcm_us_pointer(snd_pcm_ioplug_t *io)
{
	snd_pcm_us_t *us = io->private_data;
	struct usb_stream *s = us->uus->s;
	snd_pcm_sframes_t hw_pointer;

	switch (io->state) {
	case SND_PCM_STATE_RUNNING:
		VDBG("%u %u", s->periods_done, us->periods_done);
		if (s->periods_done - us->periods_done <= 1)
			hw_pointer =
				(s->periods_done - us->periods_start) & 1 ?
				io->period_size : 0;
		else
			hw_pointer = -EPIPE;

		break;
	case SND_PCM_STATE_XRUN:
		hw_pointer = -EPIPE;
		break;
	default:
		hw_pointer = 0;
		break;
	}
	VDBG("%li", hw_pointer);
	return hw_pointer;
}

static int snd_pcm_us_prepare(snd_pcm_ioplug_t *io)
{
	struct usb_stream_config	us_cfg;
	snd_pcm_us_t			*us = io->private_data;
	struct user_usb_stream		*uus = us->uus;
	int				ioctl_result, err;

	VDBG("");

	us_cfg.version = USB_STREAM_INTERFACE_VERSION;
	us_cfg.frame_size = FRAME_SIZE;
	us_cfg.sample_rate = io->rate;
	us_cfg.period_frames = io->period_size;

	ioctl_result = snd_hwdep_ioctl(us->hwdep, SNDRV_USB_STREAM_IOCTL_SET_PARAMS, &us_cfg);
	if (ioctl_result < 0) {
		perror("Couldn't configure usb_stream\n");
		return ioctl_result;
	}

	if (ioctl_result && uus && uus->s) {
		err = munmap(uus->write_area, uus->s->write_size);
		if (err < 0)
			return -errno;
		err = munmap(uus->s, uus->s->read_size);
		if (err < 0)
			return -errno;
		uus->s = NULL;
	}

	if (!uus->s) {
		uus->s = mmap(NULL, sizeof(struct usb_stream),
			 PROT_READ,
			 MAP_SHARED, us->pfd.fd,
			 0);
		if (MAP_FAILED == uus->s) {
			perror("ALSA/USX2Y: mmap");
			return -errno;
		}

		VDBG("%p %lx %i", uus->s, uus->s, uus->s->read_size);

		if (memcmp(&uus->s->cfg, &us_cfg, sizeof(us_cfg))) {
			perror("usb_stream Configuration error usb_stream\n");
			return -EIO;
		}


		uus->s = mremap(uus->s, sizeof(struct usb_stream), uus->s->read_size, MREMAP_MAYMOVE);
		if (MAP_FAILED == uus->s) {
			perror("ALSA/USX2Y: mmap");
			return -EPERM;
		}

		VDBG("%p %lx %i", uus->s, uus->s, uus->s->read_size);

		uus->write_area = mmap(NULL, uus->s->write_size,
				       PROT_READ|PROT_WRITE,
				       MAP_SHARED, us->pfd.fd,
				       (uus->s->read_size + 4095) & ~4095);
		if (MAP_FAILED == uus->write_area) {
			perror("ALSA/USX2Y: mmap");
			return -1;
		}
		VDBG("%p %i", uus->write_area, uus->s->write_size);
	}

	if (uus->s->state != usb_stream_ready)
		return -EIO;

	if (poll(&us->pfd, 1, 500000) < 0)
		return -errno;

	return 0;
}

static int snd_pcm_us_start(snd_pcm_ioplug_t *io)
{
	snd_pcm_us_t *us = io->private_data;
	VDBG("%u", us->uus->s->periods_done);

	us->periods_start = us->periods_done = us->uus->s->periods_done;
	
	return 0;
}

static int snd_pcm_us_stop(snd_pcm_ioplug_t *io)
{
	snd_pcm_us_t *us = io->private_data;

	if (!us->uus->s)
	  return 0;

	VDBG("%u", us->uus->s->periods_done);
	if (io->stream == SND_PCM_STREAM_PLAYBACK)
		memset(us->uus->write_area, 0, us->uus->s->write_size);

	return 0;
}

static snd_pcm_sframes_t snd_pcm_us_write(snd_pcm_ioplug_t *io,
				   const snd_pcm_channel_area_t *areas,
				   snd_pcm_uframes_t offset,
				   snd_pcm_uframes_t size)
{
	void *playback_addr;
	snd_pcm_us_t *us = io->private_data;
	struct user_usb_stream *uus = us->uus;
	struct usb_stream *s = uus->s;
	unsigned bytes;
	void *src = areas->addr + offset * s->cfg.frame_size;

	VDBG("%li %li %i %i", offset, size, areas->first, areas->step);

	playback_addr = uus->write_area + s->outpacket[0].offset;
	memcpy(playback_addr, src, s->outpacket[0].length);
	bytes = size * s->cfg.frame_size;
	if (bytes > s->outpacket[0].length) {
		playback_addr = uus->write_area + s->outpacket[1].offset;
		memcpy(playback_addr, src + s->outpacket[0].length,
		       bytes - s->outpacket[0].length);
	}
	us->periods_done++;
	return size;
}

static int usb_stream_read(struct user_usb_stream *uus, void *to, unsigned bytes)
{
	struct usb_stream *s = uus->s;
	int p = s->inpacket_split, l = 0;
	void *i = (void *)s + s->inpacket[p].offset + s->inpacket_split_at;
	int il = s->inpacket[p].length - s->inpacket_split_at;

	do {
		if (l + il > s->period_size)
			il = s->period_size - l;
		memcpy(to + l, i, il);
		l += il;
		if (l >= s->period_size)
			break;

		p = (p + 1) % s->inpackets;
		i = (void *)s + s->inpacket[p].offset;
		il = s->inpacket[p].length;
	} while (p != s->inpacket_split);

	return l;
}

static snd_pcm_sframes_t snd_pcm_us_read(snd_pcm_ioplug_t *io,
				  const snd_pcm_channel_area_t *areas,
				  snd_pcm_uframes_t offset,
				  snd_pcm_uframes_t size)
{
	snd_pcm_us_t *us = io->private_data;
	unsigned frame_size = us->uus->s->cfg.frame_size;
	void *to = areas->addr + offset * frame_size;
	snd_pcm_uframes_t red;

	if (size) {
		if (size != us->uus->s->cfg.period_frames) {
			SNDERR("usb_stream plugin only supports period_size"
			       " long reads, sorry");
			return -EINVAL;
		}
		if (us->uus->s->periods_done - us->periods_done == 1) {
			red = usb_stream_read(us->uus, to, size * frame_size) /
				frame_size;
			us->periods_done++;
			return red;
		}
	} else
		if (io->state == SND_PCM_STATE_XRUN)
			return -EPIPE;
	return 0;
}

static snd_pcm_ioplug_callback_t us_playback_callback = {
	.close = snd_pcm_us_close,
	.start = snd_pcm_us_start,
	.stop = snd_pcm_us_stop,
	.transfer = snd_pcm_us_write,
	.pointer = snd_pcm_us_pointer,
	.prepare = snd_pcm_us_prepare,
};
static snd_pcm_ioplug_callback_t us_capture_callback = {
	.close = snd_pcm_us_close,
	.start = snd_pcm_us_start,
	.stop = snd_pcm_us_stop,
	.transfer = snd_pcm_us_read,
	.pointer = snd_pcm_us_pointer,
	.prepare = snd_pcm_us_prepare,
};

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

static int us_set_hw_constraint(snd_pcm_us_t *us)
{
	unsigned access_list[] = {
		SND_PCM_ACCESS_MMAP_INTERLEAVED,
	};
	unsigned format_list[] = {
		SND_PCM_FORMAT_S24_3LE,
	};

	int err;
	unsigned int rate_min = us->rate ? us->rate : 44100,
		rate_max = us->rate ? us->rate : 96000,
		period_bytes_min = us->period_size ? FRAME_SIZE * us->period_size : 128,
		period_bytes_max = us->period_size ? FRAME_SIZE * us->period_size : 64*4096;

	if ((err = snd_pcm_ioplug_set_param_list(&us->io, SND_PCM_IOPLUG_HW_ACCESS,
						 ARRAY_SIZE(access_list), access_list)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_list(&us->io, SND_PCM_IOPLUG_HW_FORMAT,
						 ARRAY_SIZE(format_list), format_list)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&us->io, SND_PCM_IOPLUG_HW_CHANNELS,
						   us->channels, us->channels)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&us->io, SND_PCM_IOPLUG_HW_RATE,
						   rate_min, rate_max)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&us->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
						   period_bytes_min, period_bytes_max)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&us->io, SND_PCM_IOPLUG_HW_PERIODS,
						   2, 2)) < 0)
		return err;

	return 0;
}

static int snd_pcm_us_open(snd_pcm_t **pcmp, const char *name,
				   const char *card,
				   snd_pcm_stream_t stream, int mode,
				   snd_pcm_uframes_t period_size,
				   unsigned int rate)
{
	snd_pcm_us_t *us;
	int err;
	char us_name[32];

	if (strlen(card) >= LCARD)
		return -EINVAL;

	assert(pcmp);
	us = calloc(1, sizeof(*us));
	if (!us)
		return -ENOMEM;

	if (snprintf(us_name, sizeof(us_name), "hw:%s", card)
	    >= (int)sizeof(us_name)) {
		fprintf(stderr, "%s: WARNING: USB_STREAM client name '%s' truncated to %d characters, might not be unique\n",
			__func__, us_name, (int)strlen(us_name));
	}
	VDBG("%i %s", stream, us_name);
	us->uus = get_uus(card);
	if (!us->uus)
		return -ENOMEM;
	err = snd_hwdep_open(&us->hwdep, us_name, O_RDWR);
	if (err < 0) {
		us_free(us);
		return err;
	}
	snd_hwdep_poll_descriptors(us->hwdep, &us->pfd, 1);

	us->channels = 2;
	us->period_size = period_size;
	us->rate = rate;

	us->io.version = SND_PCM_IOPLUG_VERSION;
	us->io.name = "ALSA <-> USB_STREAM PCM I/O Plugin";
	us->io.callback = stream == SND_PCM_STREAM_PLAYBACK ?
		&us_playback_callback : &us_capture_callback;
	us->io.private_data = us;
 	us->io.mmap_rw = 0;
	us->io.poll_fd = us->pfd.fd;
	us->io.poll_events = stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;

	err = snd_pcm_ioplug_create(&us->io, name, stream, mode);
	if (err < 0) {
		us_free(us);
		return err;
	}

	err = us_set_hw_constraint(us);
	if (err < 0) {
		snd_pcm_ioplug_delete(&us->io);
		us_free(us);
		return err;
	}

	VDBG("");
	*pcmp = us->io.pcm;

	return 0;
}


SND_PCM_PLUGIN_DEFINE_FUNC(usb_stream)
{
	snd_config_iterator_t i, next;
	const char *card;
	int err;
	long period_size = 0, rate = 0;
	
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;

		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "card") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_STRING) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			snd_config_get_string(n, &card);
			continue;
		}
		if (strcmp(id, "period_size") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_INTEGER) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			snd_config_get_integer(n, &period_size);
			continue;
		}
		if (strcmp(id, "rate") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_INTEGER) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			snd_config_get_integer(n, &rate);
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	err = snd_pcm_us_open(pcmp, name, card, stream, mode, period_size, rate);

	return err;
}

SND_PCM_PLUGIN_SYMBOL(usb_stream);
