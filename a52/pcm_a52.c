/*
 * A52 Output Plugin
 *
 * Copyright (c) 2006 by Takashi Iwai <tiwai@suse.de>
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
#include <string.h>
#define __USE_XOPEN
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/pcm_plugin.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

/* some compatibility wrappers */
#ifndef AV_VERSION_INT
#define AV_VERSION_INT(a, b, c) (((a) << 16) | ((b) << 8) | (c))
#endif
#ifndef LIBAVCODEC_VERSION_INT
#define LIBAVCODEC_VERSION_INT  AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, \
                                               LIBAVCODEC_VERSION_MINOR, \
                                               LIBAVCODEC_VERSION_MICRO)
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53, 34, 0)
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#define USE_AVCODEC_FRAME
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 0, 0)
#ifndef AV_CH_LAYOUT_STEREO
#define AV_CH_LAYOUT_STEREO	CH_LAYOUT_STEREO
#define AV_CH_LAYOUT_QUAD	CH_LAYOUT_QUAD
#define AV_CH_LAYOUT_5POINT1	CH_LAYOUT_5POINT1
#endif
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 95, 0)
#ifndef AV_SAMPLE_FMT_S16
#define AV_SAMPLE_FMT_S16	SAMPLE_FMT_S16
#endif
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 25, 0)
#define AV_CODEC_ID_AC3 CODEC_ID_AC3
#endif

#if LIBAVCODEC_VERSION_INT < 0x371c01
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

struct a52_ctx {
	snd_pcm_ioplug_t io;
	snd_pcm_t *slave;
	AVCodec *codec;
	AVCodecContext *avctx;
	snd_pcm_format_t format;
	int av_format;
	unsigned int channels;
	unsigned int rate;
	unsigned int bitrate;
	short *inbuf;
	unsigned char *outbuf;
	int outbuf_size;
	snd_pcm_uframes_t transfer;
	int remain;
	int filled;
	unsigned int slave_period_size;
	unsigned int slave_buffer_size;
	snd_pcm_hw_params_t *hw_params;
#ifdef USE_AVCODEC_FRAME
	AVFrame *frame;
	int is_planar;
#endif
};

#ifdef USE_AVCODEC_FRAME
#define use_planar(rec)		(rec)->is_planar
#else
#define use_planar(rec)		0
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53, 34, 0)
static int do_encode(struct a52_ctx *rec)
{
	AVPacket pkt = {
		.data = rec->outbuf + 8,
		.size = rec->outbuf_size - 8
	};
	int got_frame;

	avcodec_encode_audio2(rec->avctx, &pkt, rec->frame, &got_frame);
	return pkt.size;
}
#else
static int do_encode(struct a52_ctx *rec)
{
	return avcodec_encode_audio(rec->avctx, rec->outbuf + 8,
				    rec->outbuf_size - 8,
				    rec->inbuf);
}
#endif

/* convert the PCM data to A52 stream in IEC958 */
static void convert_data(struct a52_ctx *rec)
{
	int out_bytes = do_encode(rec);

	rec->outbuf[0] = 0xf8; /* sync words */
	rec->outbuf[1] = 0x72;
	rec->outbuf[2] = 0x4e;
	rec->outbuf[3] = 0x1f;
	rec->outbuf[4] = rec->outbuf[13] & 7; /* bsmod */
	rec->outbuf[5] = 0x01; /* data type */
	rec->outbuf[6] = ((out_bytes * 8) >> 8) & 0xff;
	rec->outbuf[7] = (out_bytes * 8) & 0xff;
	/* swap bytes for little-endian 16bit */
	if (rec->format == SND_PCM_FORMAT_S16_LE)
		swab(rec->outbuf, rec->outbuf, out_bytes + 8);
	memset(rec->outbuf +  8 + out_bytes, 0,
	       rec->outbuf_size - 8 - out_bytes);
	rec->remain = rec->outbuf_size / 4;
	rec->filled = 0;
}

/* write pending encoded data to the slave pcm */
static int write_out_pending(snd_pcm_ioplug_t *io, struct a52_ctx *rec)
{
	int err, ofs = 0;

	if (! rec->remain)
		return 0;

	while (rec->remain) {
		err = snd_pcm_writei(rec->slave, rec->outbuf + ofs, rec->remain);
		if (err < 0) {
			if (err == -EPIPE)
				io->state = SND_PCM_STATE_XRUN;
			return err;
		} else if (! err)
			break;
		if (err < rec->remain)
			ofs += (rec->remain - err) * 4;
		rec->remain -= err;
	}
	if (rec->remain && ofs)
		memmove(rec->outbuf, rec->outbuf + ofs, rec->remain * 4);
	return 0;
}

/*
 * drain callback
 */
#ifdef USE_AVCODEC_FRAME
static void clear_remaining_planar_data(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	unsigned int i;

	for (i = 0; i < io->channels; i++)
		memset(rec->frame->data[i] + rec->filled * 2, 0,
		       (rec->avctx->frame_size - rec->filled) * 2);
}
#else
#define clear_remaining_planar_data(io) /*NOP*/
#endif

static int a52_drain(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	int err;

	if (rec->filled) {
		if ((err = write_out_pending(io, rec)) < 0)
			return err;
		/* remaining data must be converted and sent out */
		if (use_planar(rec))
			clear_remaining_planar_data(io);
		else {
			memset(rec->inbuf + rec->filled * io->channels, 0,
			       (rec->avctx->frame_size - rec->filled) * io->channels * 2);
		}
		convert_data(rec);
	}
	err = write_out_pending(io, rec);
	if (err < 0)
		return err;

	return snd_pcm_drain(rec->slave);
}

/* check whether the areas consist of a continuous interleaved stream */
static int check_interleaved(const snd_pcm_channel_area_t *areas,
			     unsigned int channels)
{
	unsigned int ch;

	if (channels > 4) /* we need re-routing for 6 channels */
		return 0;

	for (ch = 0; ch < channels; ch++) {
		if (areas[ch].addr != areas[0].addr ||
		    areas[ch].first != ch * 16 ||
		    areas[ch].step != channels * 16)
			return 0;
	}
	return 1;
}

/* Fill the input PCM to the internal buffer until a52 frames,
 * then covert and write it out.
 *
 * Returns the number of processed frames.
 */
static int fill_data(snd_pcm_ioplug_t *io,
		     const snd_pcm_channel_area_t *areas,
		     unsigned int offset, unsigned int size,
		     int interleaved)
{
	struct a52_ctx *rec = io->private_data;
	unsigned int len = rec->avctx->frame_size - rec->filled;
	short *src, *dst;
	unsigned int src_step;
	int err;
	static unsigned int ch_index[3][6] = {
		{ 0, 1 },
		{ 0, 1, 2, 3 },
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(52, 26, 0)
		/* current libavcodec expects SMPTE order */
		{ 0, 1, 4, 5, 2, 3 },
#else
		/* libavcodec older than r18540 expects A52 order */
		{ 0, 4, 1, 2, 3, 5 },
#endif
	};

	if ((err = write_out_pending(io, rec)) < 0)
		return err;

	if (size > len)
		size = len;

	dst = rec->inbuf + rec->filled * io->channels;
	if (!use_planar(rec) && interleaved) {
		memcpy(dst, areas->addr + offset * io->channels * 2,
		       size * io->channels * 2);
	} else {
		unsigned int i, ch, dst_step;
		short *dst1;

		/* flatten copy to n-channel interleaved */
		dst_step = io->channels;
		for (ch = 0; ch < io->channels; ch++, dst++) {
			const snd_pcm_channel_area_t *ap;
			ap = &areas[ch_index[io->channels / 2 - 1][ch]];
			src = (short *)(ap->addr +
					(ap->first + offset * ap->step) / 8);

#ifdef USE_AVCODEC_FRAME
			if (use_planar(rec)) {
				memcpy(rec->frame->data[ch], src, size * 2);
				continue;
			}
#endif
			dst1 = dst;
			src_step = ap->step / 16; /* in word */
			for (i = 0; i < size; i++) {
				*dst1 = *src;
				src += src_step;
				dst1 += dst_step;
			}
		}
	}
	rec->filled += size;
	if (rec->filled == rec->avctx->frame_size) {
		convert_data(rec);
		write_out_pending(io, rec);
	}
	return (int)size;
}

/*
 * transfer callback
 */
static snd_pcm_sframes_t a52_transfer(snd_pcm_ioplug_t *io,
				      const snd_pcm_channel_area_t *areas,
				      snd_pcm_uframes_t offset,
				      snd_pcm_uframes_t size)
{
	struct a52_ctx *rec = io->private_data;
	snd_pcm_sframes_t result = 0;
	int err = 0;
	int interleaved = check_interleaved(areas, io->channels);

	do {
		err = fill_data(io, areas, offset, size, interleaved);
		if (err < 0)
			break;
		offset += (unsigned int)err;
		size -= (unsigned int)err;
		result += err;
		rec->transfer += err;
	} while (size);
	return result > 0 ? result : err;
}

/*
 * pointer callback
 *
 * Calculate the current position from the delay of slave PCM
 */
static snd_pcm_sframes_t a52_pointer(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	snd_pcm_sframes_t delay;
	snd_pcm_state_t state;
	int err;

	state = snd_pcm_state(rec->slave);
	switch (state) {
	case SND_PCM_STATE_RUNNING:
	case SND_PCM_STATE_DRAINING:
		if ((err = snd_pcm_delay(rec->slave, &delay)) < 0)
			return err;
		break;
	case SND_PCM_STATE_XRUN:
	case SND_PCM_STATE_SUSPENDED:
		return -EPIPE;
	default:
		return 0;
	}

	if (delay < 0 || delay >= (snd_pcm_sframes_t)rec->slave_buffer_size)
		delay = 0;
	delay = (snd_pcm_sframes_t)io->appl_ptr - delay;
	if (delay < 0) {
		delay += io->buffer_size;
		if (delay < 0)
			delay = 0;
	}
	delay %= io->buffer_size;
	return delay;
}

/* set up the fixed parameters of slave PCM hw_parmas */
static int a52_slave_hw_params_half(struct a52_ctx *rec)
{
	int err;

	if ((err = snd_pcm_hw_params_malloc(&rec->hw_params)) < 0)
		return err;

	if ((err = snd_pcm_hw_params_any(rec->slave, rec->hw_params)) < 0) {
		SNDERR("Cannot get slave hw_params");
		goto out;
	}
	if ((err = snd_pcm_hw_params_set_access(rec->slave, rec->hw_params,
						SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		SNDERR("Cannot set slave access RW_INTERLEAVED");
		goto out;
	}
	if ((err = snd_pcm_hw_params_set_channels(rec->slave, rec->hw_params, 2)) < 0) {
		SNDERR("Cannot set slave channels 2");
		goto out;
	}
	if ((err = snd_pcm_hw_params_set_format(rec->slave, rec->hw_params,
						rec->format)) < 0) {
		SNDERR("Cannot set slave format");
		goto out;
	}
	if ((err = snd_pcm_hw_params_set_rate(rec->slave, rec->hw_params, rec->rate, 0)) < 0) {
		SNDERR("Cannot set slave rate %d", rec->rate);
		goto out;
	}
	return 0;

 out:
	free(rec->hw_params);
	rec->hw_params = NULL;
	return err;
}

/*
 * hw_params callback
 *
 * Set up slave PCM according to the current parameters
 */
static int a52_hw_params(snd_pcm_ioplug_t *io,
			 snd_pcm_hw_params_t *params ATTRIBUTE_UNUSED)
{
	struct a52_ctx *rec = io->private_data;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t buffer_size;
	int err;

	if (! rec->hw_params) {
		err = a52_slave_hw_params_half(rec);
		if (err < 0)
			return err;
	}
	period_size = io->period_size;
	if ((err = snd_pcm_hw_params_set_period_size_near(rec->slave, rec->hw_params,
							  &period_size, NULL)) < 0) {
		SNDERR("Cannot set slave period size %ld", period_size);
		return err;
	}
	buffer_size = io->buffer_size;
	if ((err = snd_pcm_hw_params_set_buffer_size_near(rec->slave, rec->hw_params,
							  &buffer_size)) < 0) {
		SNDERR("Cannot set slave buffer size %ld", buffer_size);
		return err;
	}
	if ((err = snd_pcm_hw_params(rec->slave, rec->hw_params)) < 0) {
		SNDERR("Cannot set slave hw_params");
		return err;
	}
	rec->slave_period_size = period_size;
	rec->slave_buffer_size = buffer_size;

	return 0;
}

/*
 * hw_free callback
 */
static int a52_hw_free(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;

	free(rec->hw_params);
	rec->hw_params = NULL;
	return snd_pcm_hw_free(rec->slave);
}

/*
 * sw_params callback
 *
 * Set up slave PCM sw_params
 */
static int a52_sw_params(snd_pcm_ioplug_t *io, snd_pcm_sw_params_t *params)
{
	struct a52_ctx *rec = io->private_data;
	snd_pcm_sw_params_t *sparams;
	snd_pcm_uframes_t avail_min, start_threshold;
	int len;

	snd_pcm_sw_params_get_avail_min(params, &avail_min);
	snd_pcm_sw_params_get_start_threshold(params, &start_threshold);

	len = avail_min;
	len += (int)rec->slave_buffer_size - (int)io->buffer_size;
	if (len < 0)
		avail_min = 1;
	else
		avail_min = len;
	snd_pcm_sw_params_alloca(&sparams);
	snd_pcm_sw_params_current(rec->slave, sparams);
	snd_pcm_sw_params_set_avail_min(rec->slave, sparams, avail_min);
	snd_pcm_sw_params_set_start_threshold(rec->slave, sparams,
					      start_threshold);

	return snd_pcm_sw_params(rec->slave, sparams);
}

/*
 * start and stop callbacks - just trigger slave PCM
 */
static int a52_start(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;

	/* When trying to start a PCM that's already running, the result is
	   EBADFD. We might have implicitly started the buffer by filling it
	   up, so just ignore this request if we're already running. */
	if (snd_pcm_state(rec->slave) == SND_PCM_STATE_RUNNING)
		return 0;

	return snd_pcm_start(rec->slave);
}

static int a52_stop(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;

	return snd_pcm_drop(rec->slave);
}

/* release resources */
static void a52_free(struct a52_ctx *rec)
{
	if (rec->avctx) {
		avcodec_close(rec->avctx);
		av_free(rec->avctx);
		rec->avctx = NULL;
	}

#ifdef USE_AVCODEC_FRAME
	if (rec->frame) {
		av_freep(&rec->frame->data[0]);
		rec->inbuf = NULL;
	}
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(54, 28, 0)
	av_frame_free(&rec->frame);
#else
	av_freep(&rec->frame);
#endif
#endif

	free(rec->inbuf);
	rec->inbuf = NULL;
	free(rec->outbuf);
	rec->outbuf = NULL;
}

/*
 * prepare callback
 *
 * Allocate internal buffers and set up libavcodec
 */

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(52, 3, 0)
static void set_channel_layout(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	switch (io->channels) {
	case 2:
		rec->avctx->channel_layout = AV_CH_LAYOUT_STEREO;
		break;
	case 4:
		rec->avctx->channel_layout = AV_CH_LAYOUT_QUAD;
		break;
	case 6:
		rec->avctx->channel_layout = AV_CH_LAYOUT_5POINT1;
		break;
	default:
		break;
	}
}
#else
#define set_channel_layout(io) /* NOP */
#endif

static int alloc_input_buffer(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
#ifdef USE_AVCODEC_FRAME
	rec->frame = av_frame_alloc();
	if (!rec->frame)
		return -ENOMEM;
	if (av_samples_alloc(rec->frame->data, rec->frame->linesize,
			     io->channels, rec->avctx->frame_size,
			     rec->avctx->sample_fmt, 0) < 0)
		return -ENOMEM;
	rec->frame->nb_samples = rec->avctx->frame_size;
	rec->inbuf = (short *)rec->frame->data[0];
#else
	rec->inbuf = malloc(rec->avctx->frame_size * 2 * io->channels);
#endif
	if (!rec->inbuf)
		return -ENOMEM;
	return 0;
}

static int a52_prepare(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	int err;

	a52_free(rec);

#ifdef USE_AVCODEC_FRAME
	rec->avctx = avcodec_alloc_context3(rec->codec);
#else
	rec->avctx = avcodec_alloc_context();
#endif
	if (!rec->avctx)
		return -ENOMEM;

	rec->avctx->bit_rate = rec->bitrate * 1000;
	rec->avctx->sample_rate = io->rate;
	rec->avctx->channels = io->channels;
	rec->avctx->sample_fmt = rec->av_format;

	set_channel_layout(io);


#ifdef USE_AVCODEC_FRAME
	err = avcodec_open2(rec->avctx, rec->codec, NULL);
#else
	err = avcodec_open(rec->avctx, rec->codec);
#endif
	if (err < 0)
		return -EINVAL;

	rec->outbuf_size = rec->avctx->frame_size * 4;
	rec->outbuf = malloc(rec->outbuf_size);
	if (! rec->outbuf)
		return -ENOMEM;

	if (alloc_input_buffer(io))
		return -ENOMEM;

	rec->transfer = 0;
	rec->remain = 0;
	rec->filled = 0;

	return snd_pcm_prepare(rec->slave);
}

/*
 * poll-related callbacks - just pass to slave PCM
 */
static int a52_poll_descriptors_count(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;
	return snd_pcm_poll_descriptors_count(rec->slave);
}

static int a52_poll_descriptors(snd_pcm_ioplug_t *io, struct pollfd *pfd,
				unsigned int space)
{
	struct a52_ctx *rec = io->private_data;
	return snd_pcm_poll_descriptors(rec->slave, pfd, space);
}

static int a52_poll_revents(snd_pcm_ioplug_t *io, struct pollfd *pfd,
			    unsigned int nfds, unsigned short *revents)
{
	struct a52_ctx *rec = io->private_data;
	return snd_pcm_poll_descriptors_revents(rec->slave, pfd, nfds, revents);
}

/*
 * close callback
 */
static int a52_close(snd_pcm_ioplug_t *io)
{
	struct a52_ctx *rec = io->private_data;

	a52_free(rec);
	if (rec->slave)
		return snd_pcm_close(rec->slave);
	return 0;
}
			      
#if SND_PCM_IOPLUG_VERSION >= 0x10002
static unsigned int chmap4[4] = {
	SND_CHMAP_FL, SND_CHMAP_FR,
	SND_CHMAP_RL, SND_CHMAP_RR,
};
static unsigned int chmap6[6] = {
	SND_CHMAP_FL, SND_CHMAP_FR,
	SND_CHMAP_RL, SND_CHMAP_RR,
	SND_CHMAP_FC, SND_CHMAP_LFE,
};

static snd_pcm_chmap_query_t **a52_query_chmaps(snd_pcm_ioplug_t *io ATTRIBUTE_UNUSED)
{
	snd_pcm_chmap_query_t **maps;
	int i;

	maps = calloc(4, sizeof(void *));
	if (!maps)
		return NULL;
	for (i = 0; i < 3; i++) {
		snd_pcm_chmap_query_t *p;
		p = maps[i] = calloc((i + 1) * 2 + 2, sizeof(int));
		if (!p) {
			snd_pcm_free_chmaps(maps);
			return NULL;
		}
		p->type = SND_CHMAP_TYPE_FIXED;
		p->map.channels = (i + 1) * 2;
		memcpy(p->map.pos, i < 2 ? chmap4 : chmap6,
		       (i + 1) * 2 * sizeof(int));
	}
	return maps;
}

static snd_pcm_chmap_t *a52_get_chmap(snd_pcm_ioplug_t *io)
{
	snd_pcm_chmap_t *map;

	if ((io->channels % 2) || io->channels < 2 || io->channels > 6)
		return NULL;
	map = malloc((io->channels + 1) * sizeof(int));
	if (!map)
		return NULL;
	map->channels = io->channels;
	memcpy(map->pos, io->channels < 6 ? chmap4 : chmap6,
	       io->channels * sizeof(int));
	return map;
}
#endif /* SND_PCM_IOPLUG_VERSION >= 0x10002 */

/*
 * callback table
 */
static snd_pcm_ioplug_callback_t a52_ops = {
	.start = a52_start,
	.stop = a52_stop,
	.pointer = a52_pointer,
	.transfer = a52_transfer,
	.close = a52_close,
	.hw_params = a52_hw_params,
	.hw_free = a52_hw_free,
	.sw_params = a52_sw_params,
	.prepare = a52_prepare,
	.drain = a52_drain,
	.poll_descriptors_count = a52_poll_descriptors_count,
	.poll_descriptors = a52_poll_descriptors,
	.poll_revents = a52_poll_revents,
#if SND_PCM_IOPLUG_VERSION >= 0x10002
	.query_chmaps = a52_query_chmaps,
	.get_chmap = a52_get_chmap,
#endif /* SND_PCM_IOPLUG_VERSION >= 0x10002 */
};

/*
 * set up h/w constraints
 *
 * set the period size identical with A52 frame size.
 * the max buffer size is calculated from the max buffer size
 * of the slave PCM
 */

#define A52_FRAME_SIZE	1536

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

static int a52_set_hw_constraint(struct a52_ctx *rec)
{
	static unsigned int accesses[] = {
		SND_PCM_ACCESS_MMAP_INTERLEAVED,
		SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
		SND_PCM_ACCESS_RW_INTERLEAVED,
		SND_PCM_ACCESS_RW_NONINTERLEAVED
	};
	static unsigned int accesses_planar[] = {
		SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
		SND_PCM_ACCESS_RW_NONINTERLEAVED
	};
	unsigned int formats[] = { SND_PCM_FORMAT_S16 };
	int err;
	snd_pcm_uframes_t buffer_max;
	unsigned int period_bytes, max_periods;

	if (use_planar(rec))
		err = snd_pcm_ioplug_set_param_list(&rec->io,
						    SND_PCM_IOPLUG_HW_ACCESS,
						    ARRAY_SIZE(accesses_planar),
						    accesses_planar);
	else
		err = snd_pcm_ioplug_set_param_list(&rec->io,
						    SND_PCM_IOPLUG_HW_ACCESS,
						    ARRAY_SIZE(accesses),
						    accesses);
	if (err < 0)
		return err;

	if ((err = snd_pcm_ioplug_set_param_list(&rec->io, SND_PCM_IOPLUG_HW_FORMAT,
						 ARRAY_SIZE(formats), formats)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&rec->io, SND_PCM_IOPLUG_HW_CHANNELS,
						   rec->channels, rec->channels)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&rec->io, SND_PCM_IOPLUG_HW_RATE,
						   rec->rate, rec->rate)) < 0)
		return err;

	if ((err = a52_slave_hw_params_half(rec)) < 0)
		return err;

	snd_pcm_hw_params_get_buffer_size_max(rec->hw_params, &buffer_max);
	period_bytes = A52_FRAME_SIZE * 2 * rec->channels;
	max_periods = buffer_max / A52_FRAME_SIZE;

	if ((err = snd_pcm_ioplug_set_param_minmax(&rec->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
						   period_bytes, period_bytes)) < 0 ||
	    (err = snd_pcm_ioplug_set_param_minmax(&rec->io, SND_PCM_IOPLUG_HW_PERIODS,
						   2, max_periods)) < 0)
		return err;

	return 0;
}

/*
 * Main entry point
 */
SND_PCM_PLUGIN_DEFINE_FUNC(a52)
{
	snd_config_iterator_t i, next;
	int err;
	const char *card = NULL;
	const char *pcm_string = NULL;
	unsigned int rate = 48000;
	unsigned int bitrate = 448;
	unsigned int channels = 6;
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	char devstr[128], tmpcard[8];
	struct a52_ctx *rec;
	
	if (stream != SND_PCM_STREAM_PLAYBACK) {
		SNDERR("a52 is only for playback");
		return -EINVAL;
	}

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "card") == 0) {
			if (snd_config_get_string(n, &card) < 0) {
				long val;
				err = snd_config_get_integer(n, &val);
				if (err < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				snprintf(tmpcard, sizeof(tmpcard), "%ld", val);
				card = tmpcard;
			}
			continue;
		}
		if (strcmp(id, "slavepcm") == 0) {
			if (snd_config_get_string(n, &pcm_string) < 0) {
				SNDERR("a52 slavepcm must be a string");
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "rate") == 0) {
			long val;
			if (snd_config_get_integer(n, &val) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			rate = val;
			if (rate != 44100 && rate != 48000) {
				SNDERR("rate must be 44100 or 48000");
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "bitrate") == 0) {
			long val;
			if (snd_config_get_integer(n, &val) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			bitrate = val;
			if (bitrate < 128 || bitrate > 1000) {
				SNDERR("Invalid bitrate value %d", bitrate);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "channels") == 0) {
			long val;
			if (snd_config_get_integer(n, &val) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			channels = val;
			if (channels != 2 && channels != 4 && channels != 6) {
				SNDERR("channels must be 2, 4 or 6");
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "format") == 0) {
			const char *str;
			err = snd_config_get_string(n, &str);
			if (err < 0) {
				SNDERR("invalid type for %s", id);
				return -EINVAL;
			}
			format = snd_pcm_format_value(str);
			if (format == SND_PCM_FORMAT_UNKNOWN) {
				SNDERR("unknown format %s", str);
				return -EINVAL;
			}
			if (format != SND_PCM_FORMAT_S16_LE &&
			    format != SND_PCM_FORMAT_S16_BE) {
				SNDERR("Only S16_LE/BE formats are allowed");
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	rec = calloc(1, sizeof(*rec));
	if (! rec) {
		SNDERR("cannot allocate");
		return -ENOMEM;
	}

	rec->rate = rate;
	rec->bitrate = bitrate;
	rec->channels = channels;
	rec->format = format;

#ifndef USE_AVCODEC_FRAME
	avcodec_init();
#endif
	avcodec_register_all();

	rec->codec = avcodec_find_encoder_by_name("ac3_fixed");
	if (rec->codec == NULL)
		rec->codec = avcodec_find_encoder_by_name("ac3");
	if (rec->codec == NULL) 
		rec->codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
	if (rec->codec == NULL) {
		SNDERR("Cannot find codec engine");
		err = -EINVAL;
		goto error;
	}

	if (! pcm_string || pcm_string[0] == '\0') {
		snprintf(devstr, sizeof(devstr),
			 "iec958:{AES0 0x%x AES1 0x%x AES2 0x%x AES3 0x%x %s%s}",
			 IEC958_AES0_CON_EMPHASIS_NONE | IEC958_AES0_NONAUDIO |
			 IEC958_AES0_CON_NOT_COPYRIGHT,
			 IEC958_AES1_CON_ORIGINAL | IEC958_AES1_CON_PCM_CODER,
			 0, rate == 48000 ? IEC958_AES3_CON_FS_48000 : IEC958_AES3_CON_FS_44100,
			 card ? " CARD " : "",
			 card ? card : "");
		err = snd_pcm_open(&rec->slave, devstr, stream, mode);
		if (err < 0)
			goto error;
		/* in case the slave doesn't support S16 format */
		err = snd_pcm_linear_open(&rec->slave, NULL, SND_PCM_FORMAT_S16,
					  rec->slave, 1);
		if (err < 0)
			goto error;
	} else {
		err = snd_pcm_open(&rec->slave, pcm_string, stream, mode);
		if (err < 0)
			goto error;
	}

	rec->io.version = SND_PCM_IOPLUG_VERSION;
	rec->io.name = "A52 Output Plugin";
	rec->io.mmap_rw = 0;
	rec->io.callback = &a52_ops;
	rec->io.private_data = rec;
#ifdef USE_AVCODEC_FRAME
	rec->av_format = rec->codec->sample_fmts[0];
	rec->is_planar = av_sample_fmt_is_planar(rec->av_format);
#else
	rec->av_format = AV_SAMPLE_FMT_S16;
#endif

	err = snd_pcm_ioplug_create(&rec->io, name, stream, mode);
	if (err < 0)
		goto error;

	if ((err = a52_set_hw_constraint(rec)) < 0) {
		snd_pcm_ioplug_delete(&rec->io);
		goto error;
	}

	*pcmp = rec->io.pcm;
	return 0;

 error:
	if (rec->slave)
		snd_pcm_close(rec->slave);
	free(rec);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(a52);
