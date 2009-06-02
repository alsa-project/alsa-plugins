/* Rate converter plugin using Public Parrot Hack
   Copyright (C) 2007 Jean-Marc Valin

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_rate.h>

#ifdef USE_LIBSPEEX
#include <speex/speex_resampler.h>
#else
#include "speex_resampler.h"
#endif

struct rate_src {
	int quality;
	unsigned int channels;
        SpeexResamplerState *st;
};

static snd_pcm_uframes_t input_frames(void *obj, snd_pcm_uframes_t frames)
{
   spx_uint32_t num, den;
   struct rate_src *rate = obj;
   if (frames == 0)
      return 0;
   speex_resampler_get_ratio(rate->st, &num, &den);
   return (snd_pcm_uframes_t)((frames*num+(den>>1))/den);
}

static snd_pcm_uframes_t output_frames(void *obj, snd_pcm_uframes_t frames)
{
   spx_uint32_t num, den;
   struct rate_src *rate = obj;
   if (frames == 0)
      return 0;
   speex_resampler_get_ratio(rate->st, &num, &den);
   return (snd_pcm_uframes_t)((frames*den+(num>>1))/num);
}

static void pcm_src_free(void *obj)
{
   struct rate_src *rate = obj;
   if (rate->st)
   {
      speex_resampler_destroy(rate->st);
      rate->st = NULL;
   }
}

static int pcm_src_init(void *obj, snd_pcm_rate_info_t *info)
{
   struct rate_src *rate = obj;
   int err;
   
   if (! rate->st || rate->channels != info->channels) {
      if (rate->st)
         speex_resampler_destroy(rate->st);
      rate->channels = info->channels;
      rate->st = speex_resampler_init_frac(rate->channels, info->in.period_size, info->out.period_size, info->in.rate, info->out.rate, rate->quality, &err);
      if (! rate->st)
         return -EINVAL;
   }

   return 0;
}

static int pcm_src_adjust_pitch(void *obj, snd_pcm_rate_info_t *info)
{
   struct rate_src *rate = obj;
   speex_resampler_set_rate_frac(rate->st, info->in.period_size, info->out.period_size, info->in.rate, info->out.rate);
   return 0;
}

static void pcm_src_reset(void *obj)
{
   struct rate_src *rate = obj;
   speex_resampler_reset_mem(rate->st);
}

static void pcm_src_convert_s16(void *obj, int16_t *dst, unsigned int dst_frames,
				const int16_t *src, unsigned int src_frames)
{
   struct rate_src *rate = obj;
   speex_resampler_process_interleaved_int(rate->st, src, &src_frames, dst, &dst_frames);
}

static void pcm_src_close(void *obj)
{
   free(obj);
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
	snd_output_printf(out, "Converter: libspeex "
#ifdef USE_LIBSPEEX
			  "(external)"
#else
			  "(builtin)"
#endif
			  "\n");
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

static int pcm_src_open(unsigned int version, void **objp,
			snd_pcm_rate_ops_t *ops, int quality)
{
	struct rate_src *rate;

#if SND_PCM_RATE_PLUGIN_VERSION < 0x010002
	if (version != SND_PCM_RATE_PLUGIN_VERSION) {
		fprintf(stderr, "Invalid rate plugin version %x\n", version);
		return -EINVAL;
	}
#endif
	rate = calloc(1, sizeof(*rate));
	if (! rate)
		return -ENOMEM;
	rate->quality = quality;

	*objp = rate;
#if SND_PCM_RATE_PLUGIN_VERSION >= 0x010002
	if (version == 0x010001)
		memcpy(ops, &pcm_src_ops, sizeof(snd_pcm_rate_old_ops_t));
	else
#endif
		*ops = pcm_src_ops;
	return 0;
}

int SND_PCM_RATE_PLUGIN_ENTRY(speexrate) (unsigned int version, void **objp,
					   snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, 3);
}

int SND_PCM_RATE_PLUGIN_ENTRY(speexrate_best) (unsigned int version, void **objp,
						snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, 10);
}

int SND_PCM_RATE_PLUGIN_ENTRY(speexrate_medium) (unsigned int version, void **objp,
						  snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, 5);
}
