/*
 * ALSA <-> Polypaudio PCM I/O plugin
 *
 * Copyright (c) 2006 by Pierre Ossman <ossman@cendio.se>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdio.h>
#include <sys/poll.h>

#include <pthread.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include "polyp.h"

typedef struct snd_pcm_polyp {
	snd_pcm_ioplug_t io;

    snd_polyp_t *p;

    char *device;

    /* Since ALSA expects a ring buffer we must do some voodoo. */
    size_t last_size;
    size_t ptr;

    size_t offset;

    pa_stream *stream;

    pa_sample_spec ss;
    unsigned int frame_size;
    pa_buffer_attr buffer_attr;

    pthread_mutex_t mutex;
} snd_pcm_polyp_t;

static void update_ptr(snd_pcm_polyp_t *pcm)
{
    size_t size;

    if (pcm->io.stream == SND_PCM_STREAM_PLAYBACK)
        size = pa_stream_writable_size(pcm->stream);
    else
        size = pa_stream_readable_size(pcm->stream) - pcm->offset;

    if (size > pcm->last_size) {
        pcm->ptr += size - pcm->last_size;
        pcm->ptr %= pcm->buffer_attr.maxlength;
    }

    pcm->last_size = size;
}

static int polyp_start(snd_pcm_ioplug_t *io)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	pa_operation *o;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    o = pa_stream_cork(pcm->stream, 0, NULL, NULL);
    assert(o);

    err = polyp_wait_operation(pcm->p, o);

    pa_operation_unref(o);

    if (err < 0) {
        err = -EIO;
        goto finish;
    }

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_stop(snd_pcm_ioplug_t *io)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	pa_operation *o;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    o = pa_stream_flush(pcm->stream, NULL, NULL);
    assert(o);

    err = polyp_wait_operation(pcm->p, o);

    pa_operation_unref(o);

    if (err < 0) {
        err = -EIO;
        goto finish;
    }

    o = pa_stream_cork(pcm->stream, 1, NULL, NULL);
    assert(o);

    err = polyp_wait_operation(pcm->p, o);

    pa_operation_unref(o);

    if (err < 0) {
        err = -EIO;
        goto finish;
    }

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

int polyp_drain(snd_pcm_ioplug_t *io)
{
    snd_pcm_polyp_t *pcm = io->private_data;
	pa_operation *o;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    o = pa_stream_drain(pcm->stream, NULL, NULL);
    assert(o);

    err = polyp_wait_operation(pcm->p, o);

    pa_operation_unref(o);

    if (err < 0) {
        err = -EIO;
        goto finish;
    }

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static snd_pcm_sframes_t polyp_pointer(snd_pcm_ioplug_t *io)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    update_ptr(pcm);

    err = polyp_start_poll(pcm->p);
    if (err < 0)
        goto finish;

	err = snd_pcm_bytes_to_frames(io->pcm, pcm->ptr);

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static snd_pcm_sframes_t polyp_write(snd_pcm_ioplug_t *io,
                                     const snd_pcm_channel_area_t *areas,
                                     snd_pcm_uframes_t offset,
                                     snd_pcm_uframes_t size)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	const char *buf;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    /* Make sure the buffer pointer is in sync */
    update_ptr(pcm);

    assert(pcm->last_size >= (size * pcm->frame_size));

	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;

    pa_stream_write(pcm->stream, buf, size * pcm->frame_size, NULL, 0, 0);

    /* Make sure the buffer pointer is in sync */
    update_ptr(pcm);

    err = size;

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static snd_pcm_sframes_t polyp_read(snd_pcm_ioplug_t *io,
                                    const snd_pcm_channel_area_t *areas,
                                    snd_pcm_uframes_t offset,
                                    snd_pcm_uframes_t size)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	void *dst_buf, *src_buf;
	size_t remain_size, frag_length;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && pcm->stream);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    /* Make sure the buffer pointer is in sync */
    update_ptr(pcm);

    remain_size = size * pcm->frame_size;

	dst_buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
    while (remain_size > 0) {
        pa_stream_peek(pcm->stream, (const void**)&src_buf, &frag_length);
        if (frag_length == 0)
            break;

        src_buf = (char*)src_buf + pcm->offset;
        frag_length -= pcm->offset;

        if (frag_length > remain_size) {
            pcm->offset += remain_size;
            frag_length = remain_size;
        } else
            pcm->offset = 0;

        memcpy(dst_buf, src_buf, frag_length);

        if (pcm->offset == 0)
            pa_stream_drop(pcm->stream);

        dst_buf = (char*)dst_buf + frag_length;
        remain_size -= frag_length;
    }

    /* Make sure the buffer pointer is in sync */
    update_ptr(pcm);

    err = size - (remain_size / pcm->frame_size);

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_pcm_poll_descriptors_count(snd_pcm_ioplug_t *io)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	int count;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p);

    count = polyp_poll_descriptors_count(pcm->p);

    pthread_mutex_unlock(&pcm->mutex);

	return count;
}

static int polyp_pcm_poll_descriptors(snd_pcm_ioplug_t *io, struct pollfd *pfd, unsigned int space)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	int err;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p);

    err = polyp_poll_descriptors(pcm->p, pfd, space);

    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_pcm_poll_revents(snd_pcm_ioplug_t *io, struct pollfd *pfd, unsigned int nfds, unsigned short *revents)
{
	snd_pcm_polyp_t *pcm = io->private_data;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p);

    err = polyp_poll_revents(pcm->p, pfd, nfds, revents);
    if (err < 0)
        goto finish;

    *revents = 0;

    /*
     * Make sure we have an up-to-date value.
     */
    update_ptr(pcm);

    /*
     * ALSA thinks in periods, not bytes, samples or frames.
     */
    if (pcm->last_size >= pcm->buffer_attr.minreq) {
        if (io->stream == SND_PCM_STREAM_PLAYBACK)
            *revents |= POLLOUT;
        else
            *revents |= POLLIN;
    }

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_prepare(snd_pcm_ioplug_t *io)
{
    snd_pcm_polyp_t *pcm = io->private_data;
    int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p);

    err = polyp_finish_poll(pcm->p);
    if (err < 0)
        goto finish;

    if (pcm->stream) {
        pa_stream_disconnect(pcm->stream);
        polyp_wait_stream_state(pcm->p, pcm->stream, PA_STREAM_TERMINATED);
        pa_stream_unref(pcm->stream);
        pcm->stream = NULL;
    }

    err = polyp_check_connection(pcm->p);
    if (err < 0)
        goto finish;

    assert(pcm->stream == NULL);

    if (io->stream == SND_PCM_STREAM_PLAYBACK)
        pcm->stream = pa_stream_new(pcm->p->context, "ALSA Playback", &pcm->ss, NULL);
    else
        pcm->stream = pa_stream_new(pcm->p->context, "ALSA Capture", &pcm->ss, NULL);
    assert(pcm->stream);

    if (io->stream == SND_PCM_STREAM_PLAYBACK)
        pa_stream_connect_playback(pcm->stream, pcm->device, &pcm->buffer_attr, 0, NULL, NULL);
    else
        pa_stream_connect_record(pcm->stream, pcm->device, &pcm->buffer_attr, 0);

    err = polyp_wait_stream_state(pcm->p, pcm->stream, PA_STREAM_READY);
    if (err < 0) {
        fprintf(stderr, "*** POLYPAUDIO: Unable to create stream.\n");
        pa_stream_unref(pcm->stream);
        pcm->stream = NULL;
        goto finish;
    }

    pcm->last_size = 0;
    pcm->ptr = 0;
    pcm->offset = 0;

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_hw_params(snd_pcm_ioplug_t *io, snd_pcm_hw_params_t *params)
{
    snd_pcm_polyp_t *pcm = io->private_data;
	int err = 0;

    assert(pcm);

    pthread_mutex_lock(&pcm->mutex);

    assert(pcm->p && !pcm->stream);

    pcm->frame_size = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;

    switch (io->format) {
    case SND_PCM_FORMAT_U8:
        pcm->ss.format = PA_SAMPLE_U8;
        break;
    case SND_PCM_FORMAT_A_LAW:
        pcm->ss.format = PA_SAMPLE_ALAW;
        break;
    case SND_PCM_FORMAT_MU_LAW:
        pcm->ss.format = PA_SAMPLE_ULAW;
        break;
    case SND_PCM_FORMAT_S16_LE:
        pcm->ss.format = PA_SAMPLE_S16LE;
        break;
    case SND_PCM_FORMAT_S16_BE:
        pcm->ss.format = PA_SAMPLE_S16BE;
        break;
    case SND_PCM_FORMAT_FLOAT_LE:
        pcm->ss.format = PA_SAMPLE_FLOAT32LE;
        break;
    case SND_PCM_FORMAT_FLOAT_BE:
        pcm->ss.format = PA_SAMPLE_FLOAT32BE;
        break;
    default:
        fprintf(stderr, "*** POLYPAUDIO: unsupported format %s\n",
            snd_pcm_format_name(io->format));
        err = -EINVAL;
        goto finish;
    }

    pcm->ss.rate = io->rate;
    pcm->ss.channels = io->channels;

    pcm->buffer_attr.maxlength = io->buffer_size * pcm->frame_size;
    pcm->buffer_attr.tlength = io->buffer_size * pcm->frame_size;
    pcm->buffer_attr.prebuf = io->period_size * pcm->frame_size;
    pcm->buffer_attr.minreq = io->period_size * pcm->frame_size;
    pcm->buffer_attr.fragsize = io->period_size * pcm->frame_size;

finish:
    pthread_mutex_unlock(&pcm->mutex);

	return err;
}

static int polyp_close(snd_pcm_ioplug_t *io)
{
	snd_pcm_polyp_t *pcm = io->private_data;

    assert(pcm);

    if (pcm->stream) {
        pa_stream_disconnect(pcm->stream);
        polyp_wait_stream_state(pcm->p, pcm->stream, PA_STREAM_TERMINATED);
        pa_stream_unref(pcm->stream);
    }

    if (pcm->p)
        polyp_free(pcm->p);

    if (pcm->device)
        free(pcm->device);

    pthread_mutex_destroy(&pcm->mutex);

	free(pcm);

	return 0;
}

static snd_pcm_ioplug_callback_t polyp_playback_callback = {
	.start = polyp_start,
	.stop = polyp_stop,
    .drain = polyp_drain,
	.pointer = polyp_pointer,
	.transfer = polyp_write,
    .poll_descriptors_count = polyp_pcm_poll_descriptors_count,
    .poll_descriptors = polyp_pcm_poll_descriptors,
    .poll_revents = polyp_pcm_poll_revents,
	.prepare = polyp_prepare,
	.hw_params = polyp_hw_params,
	.close = polyp_close,
};


static snd_pcm_ioplug_callback_t polyp_capture_callback = {
	.start = polyp_start,
	.stop = polyp_stop,
	.pointer = polyp_pointer,
	.transfer = polyp_read,
    .poll_descriptors_count = polyp_pcm_poll_descriptors_count,
    .poll_descriptors = polyp_pcm_poll_descriptors,
    .poll_revents = polyp_pcm_poll_revents,
	.prepare = polyp_prepare,
	.hw_params = polyp_hw_params,
	.close = polyp_close,
};


static int polyp_hw_constraint(snd_pcm_polyp_t *pcm)
{
    snd_pcm_ioplug_t *io = &pcm->io;

    static const snd_pcm_access_t access_list[] = {
        SND_PCM_ACCESS_RW_INTERLEAVED
    };
    static const unsigned int formats[] = {
        	SND_PCM_FORMAT_U8,
        SND_PCM_FORMAT_A_LAW,
        SND_PCM_FORMAT_MU_LAW,
        SND_PCM_FORMAT_S16_LE,
        SND_PCM_FORMAT_S16_BE,
        SND_PCM_FORMAT_FLOAT_LE,
        SND_PCM_FORMAT_FLOAT_BE
    };

    int err;

    err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
        ARRAY_SIZE(access_list), access_list);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
        ARRAY_SIZE(formats), formats);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
        1, PA_CHANNELS_MAX);
    if (err < 0)
        return err;

    /* FIXME: Investigate actual min and max */
    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
        8000, 48000);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
        8000, 48000);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
        1, 4294967295U);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS,
        2, 4294967295U);
    if (err < 0)
        return err;

    err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
        1, 4294967295U);
    if (err < 0)
        return err;

    return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(polyp)
{
	snd_config_iterator_t i, next;
	const char *server = NULL;
	const char *device = NULL;
	int err;
	snd_pcm_polyp_t *pcm;
    pthread_mutexattr_t mutexattr;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0)
			continue;
        if (strcmp(id, "server") == 0) {
            if (snd_config_get_string(n, &server) < 0) {
                SNDERR("Invalid type for %s", id);
                return -EINVAL;
            }
            continue;
        }
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

	pcm = calloc(1, sizeof(*pcm));

    if (device)
        pcm->device = strdup(device);

    pcm->p = polyp_new();
    assert(pcm->p);

    err = polyp_connect(pcm->p, server);
    if (err < 0)
        goto error;

    err = polyp_start_thread(pcm->p);
    if (err < 0)
        goto error;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&pcm->mutex, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);

	pcm->io.version = SND_PCM_IOPLUG_VERSION;
	pcm->io.name = "ALSA <-> Polypaudio PCM I/O Plugin";
	pcm->io.poll_fd = -1;
	pcm->io.poll_events = 0;
	pcm->io.mmap_rw = 0;
	pcm->io.callback = stream == SND_PCM_STREAM_PLAYBACK ?
		&polyp_playback_callback : &polyp_capture_callback;
	pcm->io.private_data = pcm;
 
	err = snd_pcm_ioplug_create(&pcm->io, name, stream, mode);
	if (err < 0)
		goto error;

    err = polyp_hw_constraint(pcm);
    if (err < 0) {
		snd_pcm_ioplug_delete(&pcm->io);
        goto error;
    }

	*pcmp = pcm->io.pcm;
	return 0;

error:
    if (pcm->p)
        polyp_free(pcm->p);

	free(pcm);

	return err;
}

SND_PCM_PLUGIN_SYMBOL(polyp);
