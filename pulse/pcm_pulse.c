/*-*- linux-c -*-*/

/*
 * ALSA <-> PulseAudio PCM I/O plugin
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

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include "pulse.h"

typedef struct snd_pcm_pulse {
	snd_pcm_ioplug_t io;

	snd_pulse_t *p;

	char *device;

	/* Since ALSA expects a ring buffer we must do some voodoo. */
	size_t last_size;
	size_t ptr;
	int underrun;
	int handle_underrun;

	size_t offset;
	int64_t written;

	pa_stream *stream;

	pa_sample_spec ss;
	size_t frame_size;
	pa_buffer_attr buffer_attr;
} snd_pcm_pulse_t;

static int check_stream(snd_pcm_pulse_t *pcm)
{
	int err;
	pa_stream_state_t state;

	assert(pcm);

	if (!pcm->p)
		return -EBADFD;

	err = pulse_check_connection(pcm->p);
	if (err < 0)
		return err;

	if (!pcm->stream)
		return -EBADFD;

	state = pa_stream_get_state(pcm->stream);
	if (!PA_STREAM_IS_GOOD(state))
		return -EIO;

	err = 0;

	return err;
}

static int update_ptr(snd_pcm_pulse_t *pcm)
{
	size_t size;

	if (pcm->io.stream == SND_PCM_STREAM_PLAYBACK)
		size = pa_stream_writable_size(pcm->stream);
	else
		size = pa_stream_readable_size(pcm->stream);

	if (size == (size_t) -1)
		return -EIO;

	if (pcm->io.stream == SND_PCM_STREAM_CAPTURE)
		size -= pcm->offset;

	/* Prevent accidental overrun of the fake ringbuffer */
	if (size > pcm->buffer_attr.tlength - pcm->frame_size)
		size = pcm->buffer_attr.tlength - pcm->frame_size;

	if (size > pcm->last_size) {
		pcm->ptr += size - pcm->last_size;
		pcm->ptr %= pcm->buffer_attr.tlength;
	}

	pcm->last_size = size;
	return 0;
  }

static int check_active(snd_pcm_pulse_t *pcm) {
	assert(pcm);

	/*
	 * ALSA thinks in periods, not bytes, samples or frames.
	 */

	if (pcm->io.stream == SND_PCM_STREAM_PLAYBACK) {
		size_t wsize;

		wsize = pa_stream_writable_size(pcm->stream);

		if (wsize == (size_t) -1)
			return -EIO;

		return wsize >= pcm->buffer_attr.minreq;
	} else {
		size_t rsize;

		rsize = pa_stream_readable_size(pcm->stream);

		if (rsize == (size_t) -1)
			return -EIO;

		return rsize >= pcm->buffer_attr.fragsize;
	}
}

static int update_active(snd_pcm_pulse_t *pcm) {
	int ret;

	assert(pcm);

	if (!pcm->p)
		return -EBADFD;

	ret = check_stream(pcm);
	if (ret < 0)
		goto finish;

	ret = check_active(pcm);

finish:

	if (ret != 0) /* On error signal the caller, too */
		pulse_poll_activate(pcm->p);
	else
		pulse_poll_deactivate(pcm->p);

	return ret;
}

static int wait_stream_state(snd_pcm_pulse_t *pcm, pa_stream_state_t target)
{
	pa_stream_state_t state;

	assert(pcm);

	if (!pcm->p)
		return -EBADFD;

	for (;;) {
		int err;

		err = pulse_check_connection(pcm->p);
		if (err < 0)
			return err;

		if (!pcm->stream)
			return -EBADFD;

		state = pa_stream_get_state(pcm->stream);
		if (state == target)
			break;

		if (!PA_STREAM_IS_GOOD(state))
			return -EIO;

		pa_threaded_mainloop_wait(pcm->p->mainloop);
	}

	return 0;
}

static void stream_success_cb(pa_stream * p, int success, void *userdata)
{
	snd_pcm_pulse_t *pcm = userdata;

	assert(pcm);

	if (!pcm->p)
		return;

	pa_threaded_mainloop_signal(pcm->p->mainloop, 0);
}

static int pulse_start(snd_pcm_ioplug_t * io)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	pa_operation *o, *u;
	int err = 0, err_o = 0, err_u = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	err = check_stream(pcm);
	if (err < 0)
		goto finish;

	o = pa_stream_cork(pcm->stream, 0, stream_success_cb, pcm);
	if (!o) {
		err = -EIO;
		goto finish;
	}

	u = pa_stream_trigger(pcm->stream, stream_success_cb, pcm);

	pcm->underrun = 0;
	err_o = pulse_wait_operation(pcm->p, o);
	if (u)
		err_u = pulse_wait_operation(pcm->p, u);

	pa_operation_unref(o);
	if (u)
		pa_operation_unref(u);

	if (err_o < 0 || err_u < 0) {
		err = -EIO;
		goto finish;
	}

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static int pulse_stop(snd_pcm_ioplug_t * io)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	pa_operation *o, *u;
	int err = 0, err_o = 0, err_u = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	err = check_stream(pcm);
	if (err < 0)
		goto finish;

	o = pa_stream_cork(pcm->stream, 1, stream_success_cb, pcm);
	if (!o) {
		err = -EIO;
		goto finish;
	}

	u = pa_stream_flush(pcm->stream, stream_success_cb, pcm);
	if (!u) {
		pa_operation_unref(o);
		err = -EIO;
		goto finish;
	}

	err_o = pulse_wait_operation(pcm->p, o);
	err_u = pulse_wait_operation(pcm->p, u);

	pa_operation_unref(o);
	pa_operation_unref(u);

	if (err_o < 0 || err_u < 0) {
		err = -EIO;
		goto finish;
	}

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static int pulse_drain(snd_pcm_ioplug_t * io)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	pa_operation *o;
	int err = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	err = check_stream(pcm);
	if (err < 0)
		goto finish;

	o = pa_stream_drain(pcm->stream, stream_success_cb, pcm);
	if (!o) {
		err = -EIO;
		goto finish;
	}

	err = pulse_wait_operation(pcm->p, o);

	pa_operation_unref(o);

	if (err < 0) {
		err = -EIO;
		goto finish;
	}

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static snd_pcm_sframes_t pulse_pointer(snd_pcm_ioplug_t * io)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	snd_pcm_sframes_t ret = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	if (io->state == SND_PCM_STATE_XRUN)
		return -EPIPE;

	if (io->state != SND_PCM_STATE_RUNNING)
		return 0;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	ret = check_stream(pcm);
	if (ret < 0)
		goto finish;

	if (pcm->underrun) {
		ret = -EPIPE;
		goto finish;
	}

	ret = update_ptr(pcm);
	if (ret < 0) {
		ret = -EPIPE;
		goto finish;
	}

	if (pcm->underrun)
		ret = -EPIPE;
	else
		ret = snd_pcm_bytes_to_frames(io->pcm, pcm->ptr);

finish:

	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return ret;
}

static int pulse_delay(snd_pcm_ioplug_t * io, snd_pcm_sframes_t * delayp)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	int err = 0;
	pa_usec_t lat = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	for (;;) {
		err = check_stream(pcm);
		if (err < 0)
			goto finish;

		err = pa_stream_get_latency(pcm->stream, &lat, NULL);
		if (err) {
			if (err != PA_ERR_NODATA) {
				err = -EIO;
				goto finish;
			}
		} else
			break;

		pa_threaded_mainloop_wait(pcm->p->mainloop);
	}

	*delayp =
	    snd_pcm_bytes_to_frames(io->pcm,
				    pa_usec_to_bytes(lat, &pcm->ss));

	err = 0;

finish:

	if (pcm->underrun && pcm->io.state == SND_PCM_STATE_RUNNING)
		snd_pcm_ioplug_set_state(io, SND_PCM_STATE_XRUN);

	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static snd_pcm_sframes_t pulse_write(snd_pcm_ioplug_t * io,
				     const snd_pcm_channel_area_t * areas,
				     snd_pcm_uframes_t offset,
				     snd_pcm_uframes_t size)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	const char *buf;
	snd_pcm_sframes_t ret = 0;
	size_t writebytes;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	ret = check_stream(pcm);
	if (ret < 0)
		goto finish;

	/* Make sure the buffer pointer is in sync */
	ret = update_ptr(pcm);
	if (ret < 0)
		goto finish;

	buf =
	    (char *) areas->addr + (areas->first +
				    areas->step * offset) / 8;

	writebytes = size * pcm->frame_size;
	ret = pa_stream_write(pcm->stream, buf, writebytes, NULL, 0, 0);
	if (ret < 0) {
		ret = -EIO;
		goto finish;
	}

	/* Make sure the buffer pointer is in sync */
	pcm->last_size -= writebytes;
	pcm->written += writebytes;
	ret = update_ptr(pcm);
	if (ret < 0)
		goto finish;


	ret = update_active(pcm);
	if (ret < 0)
		goto finish;

	ret = size;
	pcm->underrun = 0;

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return ret;
}

static snd_pcm_sframes_t pulse_read(snd_pcm_ioplug_t * io,
				    const snd_pcm_channel_area_t * areas,
				    snd_pcm_uframes_t offset,
				    snd_pcm_uframes_t size)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	void *dst_buf;
	size_t remain_size, frag_length;
	snd_pcm_sframes_t ret = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	ret = check_stream(pcm);
	if (ret < 0)
		goto finish;

	/* Make sure the buffer pointer is in sync */
	ret = update_ptr(pcm);
	if (ret < 0)
		goto finish;

	remain_size = size * pcm->frame_size;

	dst_buf =
	    (char *) areas->addr + (areas->first +
				    areas->step * offset) / 8;
	while (remain_size > 0) {
		const void *src_buf;

		if (pa_stream_peek(pcm->stream, &src_buf, &frag_length) < 0) {
			ret = -EIO;
			goto finish;
		}

		if (frag_length == 0)
			break;

		src_buf = (char *) src_buf + pcm->offset;
		frag_length -= pcm->offset;

		if (frag_length > remain_size) {
			pcm->offset += remain_size;
			frag_length = remain_size;
		} else
			pcm->offset = 0;

		memcpy(dst_buf, src_buf, frag_length);

		if (pcm->offset == 0)
			pa_stream_drop(pcm->stream);

		dst_buf = (char *) dst_buf + frag_length;
		remain_size -= frag_length;
		pcm->last_size -= frag_length;
	}

	/* Make sure the buffer pointer is in sync */
	ret = update_ptr(pcm);
	if (ret < 0)
		goto finish;

	ret = update_active(pcm);
	if (ret < 0)
		goto finish;

	ret = size - (remain_size / pcm->frame_size);

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return ret;
}

static void stream_state_cb(pa_stream * p, void *userdata)
{
	snd_pcm_pulse_t *pcm = userdata;
	pa_stream_state_t state;

	assert(pcm);

	if (!pcm->p)
		return;

	state = pa_stream_get_state(p);
	if (!PA_STREAM_IS_GOOD(state))
		pulse_poll_activate(pcm->p);

	pa_threaded_mainloop_signal(pcm->p->mainloop, 0);
}

static void stream_request_cb(pa_stream * p, size_t length, void *userdata)
{
	snd_pcm_pulse_t *pcm = userdata;

	assert(pcm);

	if (!pcm->p)
		return;

	update_active(pcm);
}

#ifndef PA_CHECK_VERSION
#define PA_CHECK_VERSION(x, y, z)	0
#endif

#if PA_CHECK_VERSION(0,99,0)
#define DEFAULT_HANDLE_UNDERRUN		1
#define do_underrun_detect(pcm, p) \
	((pcm)->written <= pa_stream_get_underflow_index(p))
#else
#define DEFAULT_HANDLE_UNDERRUN		0
#define do_underrun_detect(pcm, p)	1	/* always true */
#endif

static void stream_underrun_cb(pa_stream * p, void *userdata)
{
	snd_pcm_pulse_t *pcm = userdata;

	assert(pcm);

	if (!pcm->p)
		return;

	if (do_underrun_detect(pcm, p))
		pcm->underrun = 1;
}

static void stream_latency_cb(pa_stream *p, void *userdata) {
	snd_pcm_pulse_t *pcm = userdata;

	assert(pcm);

	if (!pcm->p)
		return;

	pa_threaded_mainloop_signal(pcm->p->mainloop, 0);
}

static int pulse_pcm_poll_revents(snd_pcm_ioplug_t * io,
				  struct pollfd *pfd, unsigned int nfds,
				  unsigned short *revents)
{
	int err = 0;
	snd_pcm_pulse_t *pcm = io->private_data;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	err = check_stream(pcm);
	if (err < 0)
		goto finish;

	err = check_active(pcm);
	if (err < 0)
		goto finish;

	if (err > 0)
		*revents = io->stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	else
		*revents = 0;

finish:

	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static int pulse_prepare(snd_pcm_ioplug_t * io)
{
	pa_channel_map map;
	snd_pcm_pulse_t *pcm = io->private_data;
	int err = 0, r;
	unsigned c, d;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	if (pcm->stream) {
		pa_stream_disconnect(pcm->stream);
		wait_stream_state(pcm, PA_STREAM_TERMINATED);
		pa_stream_unref(pcm->stream);
		pcm->stream = NULL;
	}

	err = pulse_check_connection(pcm->p);
	if (err < 0)
		goto finish;

	assert(pcm->stream == NULL);

	for (c = pcm->ss.channels; c > 0; c--)
		if (pa_channel_map_init_auto(&map, c, PA_CHANNEL_MAP_ALSA))
			break;

	/* Extend if nessary */
	for (d = c; d < pcm->ss.channels; d++)
		map.map[d] = PA_CHANNEL_POSITION_AUX0+(d-c);

	map.channels = pcm->ss.channels;

	if (io->stream == SND_PCM_STREAM_PLAYBACK)
		pcm->stream =
		    pa_stream_new(pcm->p->context, "ALSA Playback", &pcm->ss, &map);
	else
		pcm->stream =
		    pa_stream_new(pcm->p->context, "ALSA Capture", &pcm->ss, &map);

	if (!pcm->stream) {
		err = -ENOMEM;
		goto finish;
	}

	pa_stream_set_state_callback(pcm->stream, stream_state_cb, pcm);
	pa_stream_set_latency_update_callback(pcm->stream, stream_latency_cb, pcm);

	if (io->stream == SND_PCM_STREAM_PLAYBACK) {
		pa_stream_set_write_callback(pcm->stream,
					     stream_request_cb, pcm);
		if (pcm->handle_underrun)
			pa_stream_set_underflow_callback(pcm->stream,
							 stream_underrun_cb, pcm);
		r = pa_stream_connect_playback(pcm->stream, pcm->device,
					       &pcm->buffer_attr,
					       PA_STREAM_AUTO_TIMING_UPDATE |
					       PA_STREAM_INTERPOLATE_TIMING
#ifdef PA_STREAM_EARLY_REQUESTS
					     | PA_STREAM_EARLY_REQUESTS
#endif
					       , NULL, NULL);
	} else {
		pa_stream_set_read_callback(pcm->stream, stream_request_cb,
					    pcm);
		r = pa_stream_connect_record(pcm->stream, pcm->device,
					     &pcm->buffer_attr,
					     PA_STREAM_AUTO_TIMING_UPDATE |
					     PA_STREAM_INTERPOLATE_TIMING
#ifdef PA_STREAM_EARLY_REQUESTS
					     | PA_STREAM_EARLY_REQUESTS
#endif
			);
	}

	if (r < 0) {
		SNDERR("PulseAudio: Unable to create stream: %s\n", pa_strerror(pa_context_errno(pcm->p->context)));
		pa_stream_unref(pcm->stream);
		pcm->stream = NULL;
		r = -EIO;
		goto finish;
	}

	err = wait_stream_state(pcm, PA_STREAM_READY);
	if (err < 0) {
		SNDERR("PulseAudio: Unable to create stream: %s\n", pa_strerror(pa_context_errno(pcm->p->context)));
		pa_stream_unref(pcm->stream);
		pcm->stream = NULL;
		goto finish;
	}

	pcm->offset = 0;
	pcm->underrun = 0;
	pcm->written = 0;

	/* Reset fake ringbuffer */
	pcm->last_size = 0;
	pcm->ptr = 0;
	update_ptr(pcm);

      finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static int pulse_hw_params(snd_pcm_ioplug_t * io,
			   snd_pcm_hw_params_t * params)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	int err = 0;

	assert(pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	pcm->frame_size =
	    (snd_pcm_format_physical_width(io->format) * io->channels) / 8;

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
#ifdef PA_SAMPLE_FLOAT32LE
	case SND_PCM_FORMAT_FLOAT_LE:
		pcm->ss.format = PA_SAMPLE_FLOAT32LE;
		break;
#endif
#ifdef PA_SAMPLE_FLOAT32BE
	case SND_PCM_FORMAT_FLOAT_BE:
		pcm->ss.format = PA_SAMPLE_FLOAT32BE;
		break;
#endif
#ifdef PA_SAMPLE_S32LE
	case SND_PCM_FORMAT_S32_LE:
		pcm->ss.format = PA_SAMPLE_S32LE;
		break;
#endif
#ifdef PA_SAMPLE_S32BE
	case SND_PCM_FORMAT_S32_BE:
		pcm->ss.format = PA_SAMPLE_S32BE;
		break;
#endif
	default:
		SNDERR("PulseAudio: Unsupported format %s\n",
			snd_pcm_format_name(io->format));
		err = -EINVAL;
		goto finish;
	}

	pcm->ss.rate = io->rate;
	pcm->ss.channels = io->channels;

	pcm->buffer_attr.maxlength =
		4 * 1024 * 1024;
	pcm->buffer_attr.tlength =
		io->buffer_size * pcm->frame_size;
	pcm->buffer_attr.prebuf =
	    (io->buffer_size - io->period_size) * pcm->frame_size;
	pcm->buffer_attr.minreq = io->period_size * pcm->frame_size;
	pcm->buffer_attr.fragsize = io->period_size * pcm->frame_size;

      finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static int pulse_close(snd_pcm_ioplug_t * io)
{
	snd_pcm_pulse_t *pcm = io->private_data;

	assert(pcm);

	if (pcm->p && pcm->p->mainloop) {

		pa_threaded_mainloop_lock(pcm->p->mainloop);

		if (pcm->stream) {
			pa_stream_disconnect(pcm->stream);
			pa_stream_unref(pcm->stream);
		}

		pa_threaded_mainloop_unlock(pcm->p->mainloop);
	}

	if (pcm->p)
		pulse_free(pcm->p);

	free(pcm->device);
	free(pcm);

	return 0;
}

static int pulse_pause(snd_pcm_ioplug_t * io, int enable)
{
	snd_pcm_pulse_t *pcm = io->private_data;
	int err = 0;
	pa_operation *o;

	assert (pcm);

	if (!pcm->p || !pcm->p->mainloop)
		return -EBADFD;

	pa_threaded_mainloop_lock(pcm->p->mainloop);

	err = check_stream(pcm);
	if (err < 0)
		goto finish;

	o = pa_stream_cork(pcm->stream, enable, NULL, NULL);
	if (o)
		pa_operation_unref(o);
	else
		err = -EIO;

finish:
	pa_threaded_mainloop_unlock(pcm->p->mainloop);

	return err;
}

static const snd_pcm_ioplug_callback_t pulse_playback_callback = {
	.start = pulse_start,
	.stop = pulse_stop,
	.drain = pulse_drain,
	.pointer = pulse_pointer,
	.transfer = pulse_write,
	.delay = pulse_delay,
	.poll_revents = pulse_pcm_poll_revents,
	.prepare = pulse_prepare,
	.hw_params = pulse_hw_params,
	.close = pulse_close,
	.pause = pulse_pause
};


static const snd_pcm_ioplug_callback_t pulse_capture_callback = {
	.start = pulse_start,
	.stop = pulse_stop,
	.pointer = pulse_pointer,
	.transfer = pulse_read,
	.delay = pulse_delay,
	.poll_revents = pulse_pcm_poll_revents,
	.prepare = pulse_prepare,
	.hw_params = pulse_hw_params,
	.close = pulse_close,
};


static int pulse_hw_constraint(snd_pcm_pulse_t * pcm)
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
		SND_PCM_FORMAT_FLOAT_BE,
		SND_PCM_FORMAT_S32_LE,
		SND_PCM_FORMAT_S32_BE
	};

	int err;

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
					    ARRAY_SIZE(access_list),
					    access_list);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
					    ARRAY_SIZE(formats), formats);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
					    1, PA_CHANNELS_MAX);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
					      1, PA_RATE_MAX);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io,
					    SND_PCM_IOPLUG_HW_BUFFER_BYTES,
					    1, 4 * 1024 * 1024);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io,
					    SND_PCM_IOPLUG_HW_PERIOD_BYTES,
					    128, 2 * 1024 * 1024);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS,
					    3, 1024);
	if (err < 0)
		return err;
	return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(pulse)
{
	snd_config_iterator_t i, next;
	const char *server = NULL;
	const char *device = NULL;
	const char *fallback_name = NULL;
	int handle_underrun = DEFAULT_HANDLE_UNDERRUN;
	int err;
	snd_pcm_pulse_t *pcm;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0
		    || strcmp(id, "hint") == 0)
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
		if (strcmp(id, "handle_underrun") == 0) {
			if ((err = snd_config_get_bool(n)) < 0) {
				SNDERR("Invalid value for %s", id);
				return -EINVAL;
			}
			handle_underrun = err;
			continue;
		}
		if (strcmp(id, "fallback") == 0) {
			if (snd_config_get_string(n, &fallback_name) < 0) {
				SNDERR("Invalid value for %s", id);
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (fallback_name && name && !strcmp(name, fallback_name))
		fallback_name = NULL; /* no fallback for the same name */

	pcm = calloc(1, sizeof(*pcm));
	if (!pcm)
		return -ENOMEM;

	if (device) {
		pcm->device = strdup(device);

		if (!pcm->device) {
			err = -ENOMEM;
			goto error;
		}
	}

	pcm->p = pulse_new();
	if (!pcm->p) {
		err = -EIO;
		goto error;
	}

	pcm->handle_underrun = handle_underrun;

	err = pulse_connect(pcm->p, server, fallback_name != NULL);
	if (err < 0)
		goto error;

	pcm->io.version = SND_PCM_IOPLUG_VERSION;
	pcm->io.name = "ALSA <-> PulseAudio PCM I/O Plugin";
	pcm->io.poll_fd = pcm->p->main_fd;
	pcm->io.poll_events = POLLIN;
	pcm->io.mmap_rw = 0;
	pcm->io.callback = stream == SND_PCM_STREAM_PLAYBACK ?
	    &pulse_playback_callback : &pulse_capture_callback;
	pcm->io.private_data = pcm;

	err = snd_pcm_ioplug_create(&pcm->io, name, stream, mode);
	if (err < 0)
		goto error;

	err = pulse_hw_constraint(pcm);
	if (err < 0) {
		snd_pcm_ioplug_delete(&pcm->io);
		goto error;
	}

	*pcmp = pcm->io.pcm;
	return 0;

error:
	if (pcm->p)
		pulse_free(pcm->p);

	free(pcm->device);
	free(pcm);

	if (fallback_name)
		return snd_pcm_open_fallback(pcmp, root, fallback_name, name,
					     stream, mode);

	return err;
}

SND_PCM_PLUGIN_SYMBOL(pulse);
