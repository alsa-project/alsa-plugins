/*
 * ALSA <-> OSS PCM I/O plugin
 *
 * Copyright (c) 2005 by Takashi Iwai <tiwai@suse.de>
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

#include <stdio.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <linux/soundcard.h>

typedef struct snd_pcm_oss {
	snd_pcm_ioplug_t io;
	char *device;
	int fd;
	int fragment_set;
	int caps;
	int format;
	unsigned int period_shift;
	unsigned int periods;
	unsigned int frame_bytes;
} snd_pcm_oss_t;

static snd_pcm_sframes_t oss_write(snd_pcm_ioplug_t *io,
				   const snd_pcm_channel_area_t *areas,
				   snd_pcm_uframes_t offset,
				   snd_pcm_uframes_t size)
{
	snd_pcm_oss_t *oss = io->private_data;
	const char *buf;
	ssize_t result;

	/* we handle only an interleaved buffer */
	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
	size *= oss->frame_bytes;
	result = write(oss->fd, buf, size);
	if (result <= 0)
		return result;
	return result / oss->frame_bytes;
}

static snd_pcm_sframes_t oss_read(snd_pcm_ioplug_t *io,
				  const snd_pcm_channel_area_t *areas,
				  snd_pcm_uframes_t offset,
				  snd_pcm_uframes_t size)
{
	snd_pcm_oss_t *oss = io->private_data;
	char *buf;
	ssize_t result;

	/* we handle only an interleaved buffer */
	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
	size *= oss->frame_bytes;
	result = read(oss->fd, buf, size);
	if (result <= 0)
		return result;
	return result / oss->frame_bytes;
}

static snd_pcm_sframes_t oss_pointer(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;
	struct count_info info;
	int ptr;

	if (ioctl(oss->fd, io->stream == SND_PCM_STREAM_PLAYBACK ?
		  SNDCTL_DSP_GETOPTR : SNDCTL_DSP_GETIPTR, &info) < 0) {
		fprintf(stderr, "*** OSS: oss_pointer error\n");
		return 0;
	}
	ptr = snd_pcm_bytes_to_frames(io->pcm, info.ptr);
	return ptr;
}

static int oss_start(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;
	int tmp = io->stream == SND_PCM_STREAM_PLAYBACK ?
		PCM_ENABLE_OUTPUT : PCM_ENABLE_INPUT;

	if (ioctl(oss->fd, SNDCTL_DSP_SETTRIGGER, &tmp) < 0) {
		fprintf(stderr, "*** OSS: trigger failed\n");
		if (io->stream == SND_PCM_STREAM_CAPTURE)
			/* fake read to trigger */
			read(oss->fd, &tmp, 0);
	}
	return 0;
}

static int oss_stop(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;
	int tmp = 0;

	ioctl(oss->fd, SNDCTL_DSP_SETTRIGGER, &tmp);
	return 0;
}

static int oss_drain(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

	if (io->stream == SND_PCM_STREAM_PLAYBACK)
		ioctl(oss->fd, SNDCTL_DSP_SYNC);
	return 0;
}

static int oss_prepare(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;
	int tmp;

	ioctl(oss->fd, SNDCTL_DSP_RESET);

	tmp = io->channels;
	if (ioctl(oss->fd, SNDCTL_DSP_CHANNELS, &tmp) < 0) {
		perror("SNDCTL_DSP_CHANNELS");
		return -EINVAL;
	}
	tmp = oss->format;
	if (ioctl(oss->fd, SNDCTL_DSP_SETFMT, &tmp) < 0) {
		perror("SNDCTL_DSP_SETFMT");
		return -EINVAL;
	}
	tmp = io->rate;
	if (ioctl(oss->fd, SNDCTL_DSP_SPEED, &tmp) < 0 ||
	    tmp > io->rate * 1.01 || tmp < io->rate * 0.99) {
		perror("SNDCTL_DSP_SPEED");
		return -EINVAL;
	}
	return 0;
}

static int oss_hw_params(snd_pcm_ioplug_t *io,
			 snd_pcm_hw_params_t *params ATTRIBUTE_UNUSED)
{
	snd_pcm_oss_t *oss = io->private_data;
	int i, tmp, err;
	unsigned int period_bytes;
	long oflags, flags;

	oss->frame_bytes = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;
	switch (io->format) {
	case SND_PCM_FORMAT_U8:
		oss->format = AFMT_U8;
		break;
	case SND_PCM_FORMAT_S16_LE:
		oss->format = AFMT_S16_LE;
		break;
	case SND_PCM_FORMAT_S16_BE:
		oss->format = AFMT_S16_BE;
		break;
	default:
		fprintf(stderr, "*** OSS: unsupported format %s\n", snd_pcm_format_name(io->format));
		return -EINVAL;
	}
	period_bytes = io->period_size * oss->frame_bytes;
	oss->period_shift = 0;
	for (i = 31; i >= 4; i--) {
		if (period_bytes & (1U << i)) {
			oss->period_shift = i;
			break;
		}
	}
	if (! oss->period_shift) {
		fprintf(stderr, "*** OSS: invalid period size %d\n", (int)io->period_size);
		return -EINVAL;
	}
	oss->periods = io->buffer_size / io->period_size;

 _retry:
	tmp = oss->period_shift | (oss->periods << 16);
	if (ioctl(oss->fd, SNDCTL_DSP_SETFRAGMENT, &tmp) < 0) {
		if (! oss->fragment_set) {
			perror("SNDCTL_DSP_SETFRAGMENT");
			fprintf(stderr, "*** period shift = %d, periods = %d\n", oss->period_shift, oss->periods);
			return -EINVAL;
		}
		/* OSS has no proper way to reinitialize the fragments */
		/* try to reopen the device */
		close(oss->fd);
		oss->fd = open(oss->device, io->stream == SND_PCM_STREAM_PLAYBACK ?
			       O_WRONLY : O_RDONLY);
		if (oss->fd < 0) {
			err = -errno;
			SNDERR("Cannot reopen the device %s", oss->device);
			return err;
		}
		io->poll_fd = oss->fd;
		io->poll_events = io->stream == SND_PCM_STREAM_PLAYBACK ?
			POLLOUT : POLLIN;
		snd_pcm_ioplug_reinit_status(io);
		oss->fragment_set = 0;
		goto _retry;
	}
	oss->fragment_set = 1;

	if ((flags = fcntl(oss->fd, F_GETFL)) < 0) {
		err = -errno;
		perror("F_GETFL");
	} else {
		oflags = flags;
		if (io->nonblock)
			flags |= O_NONBLOCK;
		else
			flags &= ~O_NONBLOCK;
		if (flags != oflags &&
		    fcntl(oss->fd, F_SETFL, flags) < 0) {
			err = -errno;
			perror("F_SETFL");
		}
	}

	return 0;
}

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

static int oss_hw_constraint(snd_pcm_oss_t *oss)
{
	snd_pcm_ioplug_t *io = &oss->io; 
	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED,
		SND_PCM_ACCESS_MMAP_INTERLEAVED
	};
	unsigned int nformats;
	unsigned int format[5];
	unsigned int nchannels;
	unsigned int channel[6];
	/* period and buffer bytes must be power of two */
	static const unsigned int bytes_list[] = {
		1U<<8, 1U<<9, 1U<<10, 1U<<11, 1U<<12, 1U<<13, 1U<<14, 1U<<15,
		1U<<16, 1U<<17, 1U<<18, 1U<<19, 1U<<20, 1U<<21, 1U<<22, 1U<<23
	};
	int i, err, tmp;

	/* check trigger */
	oss->caps = 0;
	if (ioctl(oss->fd, SNDCTL_DSP_GETCAPS, &oss->caps) >= 0) {
		if (! (oss->caps & DSP_CAP_TRIGGER))
			fprintf(stderr, "*** OSS: trigger is not supported!\n");
	}

	/* access type - interleaved only */
	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
						 ARRAY_SIZE(access_list), access_list)) < 0)
		return err;

	/* supported formats */
	tmp = 0;
	ioctl(oss->fd, SNDCTL_DSP_GETFMTS, &tmp);
	nformats = 0;
	if (tmp & AFMT_U8)
		format[nformats++] = SND_PCM_FORMAT_U8;
	if (tmp & AFMT_S16_LE)
		format[nformats++] = SND_PCM_FORMAT_S16_LE;
	if (tmp & AFMT_S16_BE)
		format[nformats++] = SND_PCM_FORMAT_S16_BE;
	if (tmp & AFMT_MU_LAW)
		format[nformats++] = SND_PCM_FORMAT_MU_LAW;
	if (! nformats)
		format[nformats++] = SND_PCM_FORMAT_S16;
	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
						 nformats, format)) < 0)
		return err;
	
	/* supported channels */
	nchannels = 0;
	for (i = 0; i < 6; i++) {
		tmp = i + 1;
		if (ioctl(oss->fd, SNDCTL_DSP_CHANNELS, &tmp) >= 0)
			channel[nchannels++] = tmp;
	}
	if (! nchannels) /* assume 2ch stereo */
		err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
						      2, 2);
	else
		err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_CHANNELS,
						    nchannels, channel);
	if (err < 0)
		return err;

	/* supported rates */
	/* FIXME: should query? */
	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE, 8000, 480000);
	if (err < 0)
		return err;

	/* period size (in power of two) */
	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
					    ARRAY_SIZE(bytes_list), bytes_list);
	if (err < 0)
		return err;
	/* periods */
	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 1024);
	if (err < 0)
		return err;
	/* buffer size (in power of two) */
	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
					    ARRAY_SIZE(bytes_list), bytes_list);
	if (err < 0)
		return err;

	return 0;
}


static int oss_close(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

	close(oss->fd);
	free(oss->device);
	free(oss);
	return 0;
}

static const snd_pcm_ioplug_callback_t oss_playback_callback = {
	.start = oss_start,
	.stop = oss_stop,
	.transfer = oss_write,
	.pointer = oss_pointer,
	.close = oss_close,
	.hw_params = oss_hw_params,
	.prepare = oss_prepare,
	.drain = oss_drain,
};

static const snd_pcm_ioplug_callback_t oss_capture_callback = {
	.start = oss_start,
	.stop = oss_stop,
	.transfer = oss_read,
	.pointer = oss_pointer,
	.close = oss_close,
	.hw_params = oss_hw_params,
	.prepare = oss_prepare,
	.drain = oss_drain,
};


SND_PCM_PLUGIN_DEFINE_FUNC(oss)
{
	snd_config_iterator_t i, next;
	const char *device = "/dev/dsp";
	int err;
	snd_pcm_oss_t *oss;
	
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "device") == 0) {
			if (snd_config_get_string(n, &device) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	oss = calloc(1, sizeof(*oss));
	if (! oss) {
		SNDERR("cannot allocate");
		return -ENOMEM;
	}

	oss->device = strdup(device);
	if (oss->device == NULL) {
		SNDERR("cannot allocate");
		free(oss);
		return -ENOMEM;
	}
	oss->fd = open(device, stream == SND_PCM_STREAM_PLAYBACK ?
		       O_WRONLY : O_RDONLY);
	if (oss->fd < 0) {
		err = -errno;
		SNDERR("Cannot open device %s", device);
		goto error;
	}

	oss->io.version = SND_PCM_IOPLUG_VERSION;
	oss->io.name = "ALSA <-> OSS PCM I/O Plugin";
	oss->io.poll_fd = oss->fd;
	oss->io.poll_events = stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	oss->io.mmap_rw = 0;
	oss->io.callback = stream == SND_PCM_STREAM_PLAYBACK ?
		&oss_playback_callback : &oss_capture_callback;
	oss->io.private_data = oss;

	err = snd_pcm_ioplug_create(&oss->io, name, stream, mode);
	if (err < 0)
		goto error;

	if ((err = oss_hw_constraint(oss)) < 0) {
		snd_pcm_ioplug_delete(&oss->io);
		goto error;
	}

	*pcmp = oss->io.pcm;
	return 0;

 error:
	if (oss->fd >= 0)
		close(oss->fd);
	free(oss->device);
	free(oss);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(oss);
