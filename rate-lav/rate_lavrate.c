/*
 * Rate converter plugin using libavresample
 * Copyright (c) 2014 by Anton Khirnov
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

#include <libavresample/avresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>


static unsigned int filter_size = 16;
static unsigned int phase_shift = 10; /* auto-adjusts */
static double cutoff = 0; /* auto-adjusts */

struct rate_src {
	AVAudioResampleContext *avr;

	unsigned int in_rate;
	unsigned int out_rate;
	unsigned int channels;
};

static snd_pcm_uframes_t input_frames(void *obj ATTRIBUTE_UNUSED,
				      snd_pcm_uframes_t frames)
{
	return frames;
}

static snd_pcm_uframes_t output_frames(void *obj ATTRIBUTE_UNUSED,
				       snd_pcm_uframes_t frames)
{
	return frames;
}

static void pcm_src_free(void *obj)
{
	struct rate_src *rate = obj;
	avresample_free(&rate->avr);
}

static int pcm_src_init(void *obj, snd_pcm_rate_info_t *info)
{
	struct rate_src *rate = obj;
	int i, ir, or;

	if (!rate->avr || rate->channels != info->channels) {
		int ret;

		pcm_src_free(rate);
		rate->channels = info->channels;
		ir = rate->in_rate = info->in.rate;
		or = rate->out_rate = info->out.rate;
		i = av_gcd(or, ir);
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

		rate->avr = avresample_alloc_context();
		if (!rate->avr)
			return -ENOMEM;

		av_opt_set_int(rate->avr, "in_sample_rate",     info->in.rate,  0);
		av_opt_set_int(rate->avr, "out_sample_rate",    info->out.rate, 0);
		av_opt_set_int(rate->avr, "in_sample_format",   AV_SAMPLE_FMT_S16, 0);
		av_opt_set_int(rate->avr, "out_sample_format",  AV_SAMPLE_FMT_S16, 0);
		av_opt_set_int(rate->avr, "in_channel_layout",  av_get_default_channel_layout(rate->channels), 0);
		av_opt_set_int(rate->avr, "out_channel_layout", av_get_default_channel_layout(rate->channels), 0);

		av_opt_set_int(rate->avr, "filter_size",        filter_size, 0);
		av_opt_set_int(rate->avr, "phase_shift",        phase_shift, 0);
		av_opt_set_double(rate->avr, "cutoff",          cutoff,      0);

		ret = avresample_open(rate->avr);
		if (ret < 0) {
			avresample_free(&rate->avr);
			return -EINVAL;
		}
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

	if (rate->avr) {
#if 0
		avresample_close(rate->avr);
		avresample_open(rate->avr);
#endif
	}
}

static void pcm_src_convert_s16(void *obj, int16_t *dst,
				unsigned int dst_frames,
				const int16_t *src,
				unsigned int src_frames)
{
	struct rate_src *rate = obj;
	int chans = rate->channels;
	unsigned int total_in = avresample_get_delay(rate->avr) + src_frames;

	avresample_convert(rate->avr, (uint8_t **)&dst, dst_frames * chans * 2, dst_frames,
	                   (uint8_t **)&src, src_frames * chans * 2, src_frames);

	avresample_set_compensation(rate->avr,
                                    total_in - src_frames > filter_size ? 0 : 1, src_frames);
}

static void pcm_src_close(void *obj)
{
	pcm_src_free(obj);
}

#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
static int get_supported_rates(void *obj ATTRIBUTE_UNUSED,
			       unsigned int *rate_min,
			       unsigned int *rate_max)
{
	*rate_min = *rate_max = 0; /* both unlimited */
	return 0;
}

static void dump(void *obj ATTRIBUTE_UNUSED, snd_output_t *out)
{
	snd_output_printf(out, "Converter: libavr\n");
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
	rate->avr = NULL;
#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
	if (version == 0x010001)
		memcpy(ops, &pcm_src_ops, sizeof(snd_pcm_rate_old_ops_t));
	else
#endif
		*ops = pcm_src_ops;
	return 0;
}

int SND_PCM_RATE_PLUGIN_ENTRY(lavrate)(unsigned int version, void **objp,
			snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavrate_higher)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 64;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavrate_high)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 32;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavrate_fast)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 8;
	return pcm_src_open(version, objp, ops);
}
int SND_PCM_RATE_PLUGIN_ENTRY(lavrate_faster)(unsigned int version,
			void **objp, snd_pcm_rate_ops_t *ops)
{
	filter_size = 4;
	return pcm_src_open(version, objp, ops);
}


