/*
 * Rate converter plugin using libavcodec's resampler
 * Copyright (c) 2007 by Nicholas Kain <njkain@gmail.com>
 *
 * based on rate converter that uses libsamplerate
 * Copyright (c) 2006 by Takashi Iwai <tiwai@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <stdio.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_rate.h>
#include AVCODEC_HEADER
#include "gcd.h"

static int filter_size = 16;
static int phase_shift = 10; /* auto-adjusts */
static double cutoff = 0; /* auto-adjusts */

struct rate_src {
	struct AVResampleContext *context;
	int in_rate;
	int out_rate;
	int stored;
	int point;
	int16_t **out;
	int16_t **in;
	unsigned int channels;
};

static snd_pcm_uframes_t input_frames(void *obj, snd_pcm_uframes_t frames)
{
	return frames;
}

static snd_pcm_uframes_t output_frames(void *obj, snd_pcm_uframes_t frames)
{
	return frames;
}

static void pcm_src_free(void *obj)
{
	struct rate_src *rate = obj;
	int i;

	if (rate->out) {
		for (i=0; i<rate->channels; i++) {
			free(rate->out[i]);
		}
		free(rate->out);
	}
	if (rate->in) {
		for (i=0; i<rate->channels; i++) {
			free(rate->in[i]);
		}
		free(rate->in);
	}
	rate->out = rate->in = NULL;

	if (rate->context) {
		av_resample_close(rate->context);
		rate->context = NULL;
	}
}

static int pcm_src_init(void *obj, snd_pcm_rate_info_t *info)
{
	struct rate_src *rate = obj;
	int i, ir, or;

	if (! rate->context || rate->channels != info->channels) {
		pcm_src_free(rate);
		rate->channels = info->channels;
		ir = rate->in_rate = info->in.rate;
		or = rate->out_rate = info->out.rate;
		i = gcd(or, ir);
		if (or > ir) {
			phase_shift = or/i;
		} else {
			phase_shift = ir/i;
		}
		if (cutoff <= 0.0) {
			cutoff = 1.0 - 1.0/filter_size;
			if (cutoff < 0.80)
				cutoff = 0.80;
		}
		rate->context = av_resample_init(info->out.rate, info->in.rate,
			filter_size, phase_shift,
			(info->out.rate >= info->in.rate ? 0 : 1), cutoff);
		if (!rate->context)
			return -EINVAL;
	}

	rate->out = malloc(rate->channels * sizeof(int16_t *));
	rate->in = malloc(rate->channels * sizeof(int16_t *));
	for (i=0; i<rate->channels; i++) {
		rate->out[i] = calloc(info->out.period_size * 2, 
			sizeof(int16_t));
		rate->in[i] = calloc(info->in.period_size * 2,
			sizeof(int16_t));
	}
	rate->point = info->in.period_size / 2;
	if (!rate->out || !rate->in) {
		pcm_src_free(rate);
		return -ENOMEM;
	}

	return 0;
}

static int pcm_src_adjust_pitch(void *obj, snd_pcm_rate_info_t *info)
{
	struct rate_src *rate = obj;

	if (info->out.rate != rate->out_rate || info->in.rate != rate->in_rate)
		pcm_src_init(obj, info);
	return 0;
}

static void pcm_src_reset(void *obj)
{
	struct rate_src *rate = obj;
	rate->stored = 0;
}

static void deinterleave(const int16_t *src, int16_t **dst, unsigned int frames,
	unsigned int chans, int overflow)
{
	int i, j;

	if (chans == 1) {
		memcpy(dst + overflow, src, frames*sizeof(int16_t));
	} else if (chans == 2) {
		for (j=overflow; j<(frames + overflow); j++) {
			dst[0][j] = *(src++);
			dst[1][j] = *(src++);
		}
	} else {
		for (j=overflow; j<(frames + overflow); j++) {
			for (i=0; i<chans; i++) {
				dst[i][j] = *(src++);
			}
		}
	}
}

static void reinterleave(int16_t **src, int16_t *dst, unsigned int frames,
	unsigned int chans)
{
	int i, j;

	if (chans == 1) {
		memcpy(dst, src, frames*sizeof(int16_t));
	} else if (chans == 2) {
		for (j=0; j<frames; j++) {
			*(dst++) = src[0][j];
			*(dst++) = src[1][j];
		}
	} else {
		for (j=0; j<frames; j++) {
			for (i=0; i<chans; i++) {
				*(dst++) = src[i][j];
			}
		}
	}
}

static void pcm_src_convert_s16(void *obj, int16_t *dst, unsigned int
	dst_frames, const int16_t *src, unsigned int src_frames)
{
	struct rate_src *rate = obj;
	int consumed = 0, chans=rate->channels, ret=0, i;
	int total_in = rate->stored + src_frames, new_stored;

	deinterleave(src, rate->in, src_frames, chans, rate->point);
	for (i=0; i<chans; ++i) {	
		ret = av_resample(rate->context, rate->out[i],
				rate->in[i]+rate->point-rate->stored, &consumed,
				total_in, dst_frames, i == (chans - 1));
		new_stored = total_in-consumed;
		memmove(rate->in[i]+rate->point-new_stored,
				rate->in[i]+rate->point-rate->stored+consumed,
				new_stored*sizeof(int16_t));
	}
	av_resample_compensate(rate->context,
			total_in-src_frames>filter_size?0:1, src_frames);
	reinterleave(rate->out, dst, ret, chans);
	rate->stored = total_in-consumed;
}

static void pcm_src_close(void *obj)
{
	pcm_src_free(obj);
}

#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
static int get_supported_rates(void *obj, unsigned int *rate_min,
			       unsigned int *rate_max)
{
	*rate_min = *rate_max = 0; /* both unlimited */
	return 0;
}

static void dump(void *obj, snd_output_t *out)
{
	snd_output_printf(out, "Converter: liblavc\n");
}
#endif

static snd_pcm_rate_ops_t pcm_src_ops = {
	.close = pcm_src_close,
	.init = pcm_src_init,
	.free = pcm_src_free,
	.reset = pcm_src_reset,
	.adjust_pitch = pcm_src_adjust_pitch,
	.convert_s16 = pcm_src_convert_s16,
	.input_frames = input_frames,
	.output_frames = output_frames,
#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
	.version = SND_PCM_RATE_PLUGIN_VERSION,
	.get_supported_rates = get_supported_rates,
	.dump = dump,
#endif
};

int pcm_src_open(unsigned int version, void **objp, snd_pcm_rate_ops_t *ops)

{
	struct rate_src *rate;

#if SND_PCM_RATE_PLUGIN_VERSION < 0x010002
	if (version != SND_PCM_RATE_PLUGIN_VERSION) {
		fprintf(stderr, "Invalid rate plugin version %x\n", version);
		return -EINVAL;
	}
#endif
	rate = calloc(1, sizeof(*rate));
	if (!rate)
		return -ENOMEM;

	*objp = rate;
	rate->context = NULL;
#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
	if (version == 0x010001)
		memcpy(ops, &pcm_src_ops, sizeof(snd_pcm_rate_old_ops_t));
	else
#endif
		*ops = pcm_src_ops;
	return 0;
}

int SND_PCM_RATE_PLUGIN_ENTRY(lavcrate)(unsigned int version, void **objp,
			snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavcrate_higher)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 64;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavcrate_high)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 32;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavcrate_fast)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 8;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavcrate_faster)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 4;
	return pcm_src_open(version, objp, ops);
}


