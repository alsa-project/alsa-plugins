/*
 *  PCM - JACK plugin
 *
 *  Copyright (c) 2003 by Maarten de Boer <mdeboer@iua.upf.es>
 *                2005 Takashi Iwai <tiwai@suse.de>
 *
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
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdbool.h>
#include <byteswap.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <jack/jack.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <pthread.h>

#define MAX_PERIODS_MULTIPLE 64

typedef struct snd_pcm_jack_port_list {
	struct snd_pcm_jack_port_list *next;
	/* will always be allocated with size of the string.
	 * See snd_pcm_jack_port_list_add().
	 */
	char name[0];
} snd_pcm_jack_port_list_t;

typedef struct {
	snd_pcm_ioplug_t io;

	int fd;
	int activated;		/* jack is activated? */
	pthread_mutex_t running_mutex;
	int running;		/* jack is running? */

	snd_pcm_jack_port_list_t **port_names;
	unsigned int num_ports;
	snd_pcm_uframes_t boundary;
	snd_pcm_uframes_t hw_ptr;
	unsigned int sample_bits;
	snd_pcm_uframes_t min_avail;
	int use_period_alignment;

	snd_pcm_channel_area_t *areas;

	jack_port_t **ports;
	jack_client_t *client;

	/* JACK thread -> ALSA thread */
	bool xrun_detected;
} snd_pcm_jack_t;

/* snd_pcm_ioplug_avail() was introduced after alsa-lib 1.1.6 */
#if SND_LIB_VERSION < 0x10107
static snd_pcm_uframes_t snd_pcm_ioplug_avail(const snd_pcm_ioplug_t *io,
					      const snd_pcm_uframes_t hw_ptr,
					      const snd_pcm_uframes_t appl_ptr)
{
	return io->buffer_size - snd_pcm_ioplug_hw_avail(io, hw_ptr, appl_ptr);
}
#endif

/* adds one element to the head of the list */
static int snd_pcm_jack_port_list_add(snd_pcm_jack_t *jack,
				      const unsigned int channel,
				      const char * const name)
{
	const size_t name_size = strlen(name) + 1;
	const size_t elem_size = sizeof(snd_pcm_jack_port_list_t) + name_size;
	snd_pcm_jack_port_list_t * const elem = calloc(1, elem_size);

	if (elem == NULL)
		return -ENOMEM;

	/* Above it is guaranteed that elem->name is big enough for the size of
	 * name because strlen(name) + 1 will be used to allocate the buffer.
	 */
	strcpy(elem->name, name);
	elem->next = jack->port_names[channel];

	jack->port_names[channel] = elem;

	return 0;
}

static int pcm_poll_block_check(snd_pcm_ioplug_t *io)
{
	static char buf[32];
	snd_pcm_uframes_t avail;
	snd_pcm_jack_t *jack = io->private_data;

	if (io->state == SND_PCM_STATE_RUNNING ||
	    io->state == SND_PCM_STATE_DRAINING ||
	    (io->state == SND_PCM_STATE_PREPARED && io->stream == SND_PCM_STREAM_CAPTURE)) {
		avail = snd_pcm_ioplug_avail(io, jack->hw_ptr, io->appl_ptr);
		if (avail < jack->min_avail) {
			while (read(io->poll_fd, &buf, sizeof(buf)) == sizeof(buf))
				;
			return 1;
		}
	}

	return 0;
}

static int pcm_poll_unblock_check(snd_pcm_ioplug_t *io)
{
	static char buf[1];
	snd_pcm_uframes_t avail;
	snd_pcm_jack_t *jack = io->private_data;

	avail = snd_pcm_ioplug_avail(io, jack->hw_ptr, io->appl_ptr);
	/* In draining state poll_fd is used to wait
	 * till all pending frames are played.
	 * Therefore it has to be guarantee that a poll event is also generated
	 * if the buffer contains less than min_avail frames
	 */
	if (avail >= jack->min_avail || io->state == SND_PCM_STATE_DRAINING) {
		write(jack->fd, &buf, 1);
		return 1;
	}

	return 0;
}

static void snd_pcm_jack_free(snd_pcm_jack_t *jack)
{
	if (jack == NULL)
		return;

	if (jack->client)
		jack_client_close(jack->client);
	if (jack->port_names) {
		unsigned int i;

		for (i = 0; i < jack->num_ports; i++) {
			snd_pcm_jack_port_list_t *port_elem =
					jack->port_names[i];

			while (port_elem != NULL) {
				snd_pcm_jack_port_list_t *next_port_elem =
						port_elem->next;
				free(port_elem);
				port_elem = next_port_elem;
			}
		}
		free(jack->port_names);
		jack->port_names = NULL;
	}
	pthread_mutex_destroy (&jack->running_mutex);
	if (jack->fd >= 0)
		close(jack->fd);
	if (jack->io.poll_fd >= 0)
		close(jack->io.poll_fd);
	free(jack->areas);
	free(jack->ports);
	free(jack);
}

static int snd_pcm_jack_close(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	snd_pcm_jack_free(jack);
	return 0;
}

static int snd_pcm_jack_poll_revents(snd_pcm_ioplug_t *io,
				     struct pollfd *pfds, unsigned int nfds,
				     unsigned short *revents)
{
	assert(pfds && nfds == 1 && revents);

	*revents = pfds[0].revents & ~(POLLIN | POLLOUT);
	if (pfds[0].revents & POLLIN && !pcm_poll_block_check(io))
		*revents |= (io->stream == SND_PCM_STREAM_PLAYBACK) ? POLLOUT : POLLIN;
	return 0;
}

static snd_pcm_sframes_t snd_pcm_jack_pointer(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;

	if (jack->xrun_detected)
		return -EPIPE;

#ifdef SND_PCM_IOPLUG_FLAG_BOUNDARY_WA
	return jack->hw_ptr;
#else
	return jack->hw_ptr % io->buffer_size;
#endif
}

static int
snd_pcm_jack_process_cb(jack_nframes_t nframes, snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	snd_pcm_uframes_t xfer = 0;
	unsigned int channel;

	if (pthread_mutex_trylock (&jack->running_mutex) == EBUSY) {
		/* Note that locking should only ever fail if
		 * snd_pcm_jack_start or snd_pcm_jack_stop is called at the
		 * same time, in which case dropping the current buffer is not
		 * an issue. */
		return 0;
	}
	if (!jack->running) {
		pthread_mutex_unlock (&jack->running_mutex);
		return 0;
	}
	
	for (channel = 0; channel < io->channels; channel++) {
		jack->areas[channel].addr = 
			jack_port_get_buffer (jack->ports[channel], nframes);
		jack->areas[channel].first = 0;
		jack->areas[channel].step = jack->sample_bits;
	}

	if (io->state == SND_PCM_STATE_RUNNING ||
	    io->state == SND_PCM_STATE_DRAINING) {
		snd_pcm_uframes_t hw_ptr = jack->hw_ptr;
		const snd_pcm_uframes_t hw_avail = snd_pcm_ioplug_hw_avail(io, hw_ptr,
									   io->appl_ptr);

		if (hw_avail > 0) {
			const snd_pcm_channel_area_t *areas = snd_pcm_ioplug_mmap_areas(io);
			const snd_pcm_uframes_t offset = hw_ptr % io->buffer_size;

			xfer = nframes;
			if (xfer > hw_avail)
				xfer = hw_avail;

			if (io->stream == SND_PCM_STREAM_PLAYBACK)
				snd_pcm_areas_copy_wrap(jack->areas, 0, nframes,
							areas, offset,
							io->buffer_size,
							io->channels, xfer,
							io->format);
			else
				snd_pcm_areas_copy_wrap(areas, offset,
							io->buffer_size,
							jack->areas, 0, nframes,
							io->channels, xfer,
							io->format);

			hw_ptr += xfer;
			if (hw_ptr >= jack->boundary)
				hw_ptr -= jack->boundary;
			jack->hw_ptr = hw_ptr;
		}
	}

	/* check if requested frames were copied */
	if (xfer < nframes) {
		/* always fill the not yet written JACK buffer with silence */
		if (io->stream == SND_PCM_STREAM_PLAYBACK) {
			const snd_pcm_uframes_t frames = nframes - xfer;

			snd_pcm_areas_silence(jack->areas, xfer, io->channels,
					      frames, io->format);
		}

		if (io->state == SND_PCM_STATE_RUNNING ||
		    io->state == SND_PCM_STATE_DRAINING) {
			/* report Xrun to user application */
			jack->xrun_detected = true;
		}
	}

	pcm_poll_unblock_check(io); /* unblock socket for polling if needed */

	pthread_mutex_unlock (&jack->running_mutex);

	return 0;
}

static void jack_allocate_and_register_ports(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	unsigned int i;

	jack->ports = calloc(io->channels, sizeof(jack_port_t *));

	for (i = 0; i < io->channels; i++) {
		char port_name[32];

		if (io->stream == SND_PCM_STREAM_PLAYBACK) {
			sprintf(port_name, "out_%03d", i);
			jack->ports[i] = jack_port_register(jack->client, port_name,
							    JACK_DEFAULT_AUDIO_TYPE,
							    JackPortIsOutput, 0);
		} else {
			sprintf(port_name, "in_%03d", i);
			jack->ports[i] = jack_port_register(jack->client, port_name,
							    JACK_DEFAULT_AUDIO_TYPE,
							    JackPortIsInput, 0);
		}
	}
}

static int snd_pcm_jack_prepare(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	unsigned int i;
	snd_pcm_sw_params_t *swparams;
	int err;

	if (io->channels != jack->num_ports) {
		SNDERR("Channel count %d not equal to no. of ports %d in JACK",
		       io->channels, jack->num_ports);
		return -EINVAL;
	}

	jack->hw_ptr = 0;
	jack->xrun_detected = false;

	jack->min_avail = io->period_size;
	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_sw_params_current(io->pcm, swparams);
	if (err == 0) {
		snd_pcm_sw_params_get_avail_min(swparams, &jack->min_avail);
		/* get boundary for available calulation */
		snd_pcm_sw_params_get_boundary(swparams, &jack->boundary);
	}

	if (io->stream == SND_PCM_STREAM_PLAYBACK)
		pcm_poll_unblock_check(io); /* playback pcm initially accepts writes */
	else
		pcm_poll_block_check(io); /* block capture pcm if that's XRUN recovery */

	if (!jack->ports) {
		jack_allocate_and_register_ports(io);
		jack_set_process_callback(jack->client,
					  (JackProcessCallback)snd_pcm_jack_process_cb, io);
	}

	if (jack->activated)
		return 0;

	if (jack_activate(jack->client))
		return -EIO;

	jack->activated = 1;

	for (i = 0; i < io->channels && i < jack->num_ports; i++) {
		const char * const own_port = jack_port_name(jack->ports[i]);
		snd_pcm_jack_port_list_t *port_elem;

		for (port_elem = jack->port_names[i]; port_elem != NULL;
		     port_elem = port_elem->next) {
			const char *src, *dst;
			if (io->stream == SND_PCM_STREAM_PLAYBACK) {
				src = own_port;
				dst = port_elem->name;
			} else {
				src = port_elem->name;
				dst = own_port;
			}
			if (jack_connect(jack->client, src, dst)) {
				fprintf(stderr, "cannot connect %s to %s\n",
					src, dst);
				return -EIO;
			}
		}
	}
	return 0;
}

static int snd_pcm_jack_start(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	pthread_mutex_lock (&jack->running_mutex);
	jack->running = 1;
	pthread_mutex_unlock (&jack->running_mutex);
	/*
	 * Since the processing of jack_activate() and jack_connect() take a
	 * while longer, snd_pcm_jack_start() was blocked.
	 * Consider a usecase of reading the data from capture device and
	 * writing to a playback device, since the capture device is
	 * already started and the starting of playback device is blocked,
	 * it leads to XRUNs for capture device.
	 * Therefore these calls are moved to snd_pcm_jack_prepare(),
	 * So that the capture and playback devices can be prepared in advance
	 * and starting of the device doesn't take too long.
	 */
	return 0;
}

static int snd_pcm_jack_stop(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;
	pthread_mutex_lock (&jack->running_mutex);
	jack->running = 0;
	pthread_mutex_unlock (&jack->running_mutex);
	return 0;
}

static int snd_pcm_jack_hw_free(snd_pcm_ioplug_t *io)
{
	snd_pcm_jack_t *jack = io->private_data;

	if (jack->activated) {
		jack_deactivate(jack->client);
		jack->activated = 0;
	}
#if 0
	unsigned i;
	for (i = 0; i < io->channels; i++) {
		if (jack->ports[i]) {
			jack_port_unregister(jack->client, jack->ports[i]);
			jack->ports[i] = NULL;
		}
	}
#endif
	return 0;
}

static snd_pcm_ioplug_callback_t jack_pcm_callback = {
	.close = snd_pcm_jack_close,
	.start = snd_pcm_jack_start,
	.stop = snd_pcm_jack_stop,
	.pointer = snd_pcm_jack_pointer,
	.hw_free = snd_pcm_jack_hw_free,
	.prepare = snd_pcm_jack_prepare,
	.poll_revents = snd_pcm_jack_poll_revents,
};

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

static int jack_set_hw_constraint(snd_pcm_jack_t *jack)
{
	unsigned int access_list[] = {
		SND_PCM_ACCESS_MMAP_INTERLEAVED,
		SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
		SND_PCM_ACCESS_RW_INTERLEAVED,
		SND_PCM_ACCESS_RW_NONINTERLEAVED
	};
	unsigned int format = SND_PCM_FORMAT_FLOAT;
	unsigned int rate = jack_get_sample_rate(jack->client);
	unsigned int psize_list[MAX_PERIODS_MULTIPLE];
	unsigned int nframes = jack_get_buffer_size(jack->client);
	unsigned int jack_buffer_bytes = (snd_pcm_format_size(format, nframes) *
					  jack->num_ports);
	unsigned int i;
	int err;

	if (!jack_buffer_bytes) {
		SNDERR("Buffer size is zero");
		return -EINVAL;
	}
	for (i = 1; i <= ARRAY_SIZE(psize_list); i++)
		psize_list[i-1] = jack_buffer_bytes * i;

	jack->sample_bits = snd_pcm_format_physical_width(format);
	if ((err = snd_pcm_ioplug_set_param_list(&jack->io, SND_PCM_IOPLUG_HW_ACCESS,
						 ARRAY_SIZE(access_list), access_list)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_list(&jack->io, SND_PCM_IOPLUG_HW_FORMAT,
						 1, &format)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&jack->io, SND_PCM_IOPLUG_HW_CHANNELS,
						   jack->num_ports, jack->num_ports)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&jack->io, SND_PCM_IOPLUG_HW_RATE,
						   rate, rate)) < 0 ||
	    (err = jack->use_period_alignment ?
				snd_pcm_ioplug_set_param_list(&jack->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, ARRAY_SIZE(psize_list), psize_list) :
				snd_pcm_ioplug_set_param_minmax(&jack->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 128, 64*1024) ) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&jack->io, SND_PCM_IOPLUG_HW_PERIODS,
						   2, 64)) < 0)
		return err;

	return 0;
}

static int parse_ports(snd_pcm_jack_t *jack, snd_config_t *conf)
{
	snd_config_iterator_t i, next;
	snd_pcm_jack_port_list_t **ports = NULL;
	unsigned int cnt = 0;
	unsigned int channel;

	if (conf == NULL)
		return 0;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;

		if (snd_config_get_id(n, &id) < 0)
			continue;
		cnt++;
	}
	jack->port_names = ports = calloc(cnt, sizeof(jack->port_names[0]));
	if (ports == NULL)
		return -ENOMEM;
	jack->num_ports = cnt;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		const char *port;
		int err;

		if (snd_config_get_id(n, &id) < 0)
			continue;
		channel = atoi(id);
		if (snd_config_get_string(n, &port) >= 0) {
			err = snd_pcm_jack_port_list_add(jack, channel, port);
			if (err < 0)
				return err;
		} else if (snd_config_get_type(n) == SND_CONFIG_TYPE_COMPOUND) {
			snd_config_iterator_t k, next_k;

			snd_config_for_each(k, next_k, n) {
				snd_config_t *m = snd_config_iterator_entry(k);

				if (snd_config_get_string(m, &port) < 0)
					continue;
				err = snd_pcm_jack_port_list_add(jack, channel,
								 port);
				if (err < 0)
					return err;
			}
		} else {
			continue;
		}
	}

	return 0;
}

static int make_nonblock(int fd)
{
	int fl;

	if ((fl = fcntl(fd, F_GETFL)) < 0)
		return fl;

	if (fl & O_NONBLOCK)
		return 0;

	return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int snd_pcm_jack_open(snd_pcm_t **pcmp, const char *name,
			     const char *client_name,
			     snd_config_t *playback_conf,
			     snd_config_t *capture_conf,
			     int use_period_alignment,
			     snd_pcm_stream_t stream, int mode)
{
	snd_pcm_jack_t *jack;
	int err;
	int fd[2];
	static unsigned int num = 0;
	char jack_client_name[32];
	
	assert(pcmp);
	jack = calloc(1, sizeof(*jack));
	if (!jack)
		return -ENOMEM;

	pthread_mutex_init (&jack->running_mutex, NULL);

	jack->fd = -1;
	jack->io.poll_fd = -1;
	jack->use_period_alignment = use_period_alignment;

	err = parse_ports(jack, stream == SND_PCM_STREAM_PLAYBACK ?
			  playback_conf : capture_conf);
	if (err) {
		snd_pcm_jack_free(jack);
		return err;
	}

	if (jack->num_ports == 0) {
		SNDERR("define the %s_ports section",
		       stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture");
		snd_pcm_jack_free(jack);
		return -EINVAL;
	}

	if (client_name == NULL)
		err = snprintf(jack_client_name, sizeof(jack_client_name),
			       "alsa-jack.%s%s.%d.%d", name,
			       stream == SND_PCM_STREAM_PLAYBACK ? "P" : "C",
			       getpid(), num++);
	else
		err = snprintf(jack_client_name, sizeof(jack_client_name),
			       "%s", client_name);

	if (err >= (int)sizeof(jack_client_name)) {
		fprintf(stderr, "%s: WARNING: JACK client name '%s' truncated to %d characters, might not be unique\n",
			__func__, jack_client_name, (int)strlen(jack_client_name));
	}

	jack->client = jack_client_open(jack_client_name, JackNoStartServer, NULL);

	if (jack->client == 0) {
		snd_pcm_jack_free(jack);
		return -ENOENT;
	}

	jack->areas = calloc(jack->num_ports, sizeof(snd_pcm_channel_area_t));
	if (! jack->areas) {
		snd_pcm_jack_free(jack);
		return -ENOMEM;
	}

	socketpair(AF_LOCAL, SOCK_STREAM, 0, fd);
	
	make_nonblock(fd[0]);
	make_nonblock(fd[1]);

	jack->fd = fd[0];

	jack->io.version = SND_PCM_IOPLUG_VERSION;
	jack->io.name = "ALSA <-> JACK PCM I/O Plugin";
	jack->io.callback = &jack_pcm_callback;
	jack->io.private_data = jack;
	jack->io.poll_fd = fd[1];
	jack->io.poll_events = POLLIN;
	jack->io.mmap_rw = 1;

#ifdef SND_PCM_IOPLUG_FLAG_BOUNDARY_WA
	jack->io.flags = SND_PCM_IOPLUG_FLAG_BOUNDARY_WA;
#else
#warning hw_ptr updates of buffer_size will not be recognized by the ALSA library. Consider to update your ALSA library.
#endif

	err = snd_pcm_ioplug_create(&jack->io, name, stream, mode);
	if (err < 0) {
		snd_pcm_jack_free(jack);
		return err;
	}

	err = jack_set_hw_constraint(jack);
	if (err < 0) {
		snd_pcm_ioplug_delete(&jack->io);
		return err;
	}

	*pcmp = jack->io.pcm;

	return 0;
}


SND_PCM_PLUGIN_DEFINE_FUNC(jack)
{
	snd_config_iterator_t i, next;
	snd_config_t *playback_conf = NULL;
	snd_config_t *capture_conf = NULL;
	const char *client_name = NULL;
	int err;
	int align_jack_period = 1; /*by default we allow only JACK aligned period size*/
	
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "name") == 0) {
			snd_config_get_string(n, &client_name);
			continue;
		}
		if (strcmp(id, "playback_ports") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			playback_conf = n;
			continue;
		}
		if (strcmp(id, "capture_ports") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			capture_conf = n;
			continue;
		}
		if (strcmp(id, "align_psize") == 0) {
			err = snd_config_get_bool(n);
			if (err < 0)
				return err;
			align_jack_period = err ? 1 : 0;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	err = snd_pcm_jack_open(pcmp, name, client_name, playback_conf, capture_conf, align_jack_period, stream, mode);

	return err;
}

SND_PCM_PLUGIN_SYMBOL(jack);
