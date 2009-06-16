/*
 * Speex DSP plugin
 *
 * Copyright (c) 2009 by Takashi Iwai <tiwai@suse.de>
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

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <speex/speex_preprocess.h>
#include <speex/speex_echo.h>

/* DSP parameters */
struct spx_parms {
	int frames;
	int denoise;
	int agc;
	int echo;
	int filter_length;
	float agc_level;
	int dereverb;
	float dereverb_decay;
	float dereverb_level;
};

typedef struct {
	snd_pcm_extplug_t ext;
	struct spx_parms parms;
	/* instance and intermedate buffer */
	SpeexPreprocessState *state;
	SpeexEchoState *echo_state;
	short *buf;
	short *outbuf;
	/* running states */
	unsigned int filled;
	unsigned int processed;
} snd_pcm_speex_t;


static inline void *area_addr(const snd_pcm_channel_area_t *area,
			      snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

static snd_pcm_sframes_t
spx_transfer(snd_pcm_extplug_t *ext,
	     const snd_pcm_channel_area_t *dst_areas,
	     snd_pcm_uframes_t dst_offset,
	     const snd_pcm_channel_area_t *src_areas,
	     snd_pcm_uframes_t src_offset,
	     snd_pcm_uframes_t size)
{
	snd_pcm_speex_t *spx = (snd_pcm_speex_t *)ext;
	short *src = area_addr(src_areas, src_offset);
	short *dst = area_addr(dst_areas, dst_offset);
	unsigned int count = size;
	short *databuf;

	if (!spx->state && !spx->echo_state) {
		/* no DSP processing */
		memcpy(dst, src, count * 2);
		return size;
	}

	if (spx->echo_state)
		databuf = spx->outbuf;
	else
		databuf = spx->buf;

	while (count > 0) {
		unsigned int chunk;
		if (spx->filled + count > spx->parms.frames)
			chunk = spx->parms.frames - spx->filled;
		else
			chunk = count;
		if (spx->processed)
			memcpy(dst, databuf + spx->filled, chunk * 2);
		else
			memset(dst, 0, chunk * 2);
		dst += chunk;
		memcpy(spx->buf + spx->filled, src, chunk * 2);
		spx->filled += chunk;
		if (spx->filled == spx->parms.frames) {
			if (spx->echo_state)
				speex_echo_capture(spx->echo_state, spx->buf,
						   spx->outbuf);
			if (spx->state)
				speex_preprocess_run(spx->state, databuf);
			if (spx->echo_state)
				speex_echo_playback(spx->echo_state, databuf);
			spx->processed = 1;
			spx->filled = 0;
		}
		src += chunk;
		count -= chunk;
	}

	return size;
}

static int spx_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_speex_t *spx = (snd_pcm_speex_t *)ext;

	spx->filled = 0;
	spx->processed = 0;

	if (!spx->buf) {
		spx->buf = malloc(spx->parms.frames * 2);
		if (!spx->buf)
			return -ENOMEM;
	}
	memset(spx->buf, 0, spx->parms.frames * 2);

	if (!spx->outbuf) {
		spx->outbuf = malloc(spx->parms.frames * 2);
		if (!spx->outbuf)
			return -ENOMEM;
	}
	memset(spx->outbuf, 0, spx->parms.frames * 2);

	if (spx->state) {
		speex_preprocess_state_destroy(spx->state);
		spx->state = NULL;
	}
	if (spx->echo_state) {
		speex_echo_state_destroy(spx->echo_state);
		spx->echo_state = NULL;
	}

	if (spx->parms.echo) {
		spx->echo_state = speex_echo_state_init(spx->parms.frames,
						spx->parms.filter_length);
		if (!spx->echo_state)
			return -EIO;
		speex_echo_ctl(spx->echo_state, SPEEX_ECHO_SET_SAMPLING_RATE,
			       &spx->ext.rate);
	}

	/* no preprocessor? */
	if (!spx->parms.denoise && !spx->parms.agc && !spx->parms.dereverb)
		return 0;

	spx->state = speex_preprocess_state_init(spx->parms.frames,
						 spx->ext.rate);
	if (!spx->state)
		return -EIO;
	if (spx->echo_state)
		speex_preprocess_ctl(spx->state,
				     SPEEX_PREPROCESS_SET_ECHO_STATE,
				     spx->echo_state);

	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_DENOISE,
			     &spx->parms.denoise);
	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_AGC,
			     &spx->parms.agc);
	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_AGC_LEVEL,
			     &spx->parms.agc_level);
	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_DEREVERB,
			     &spx->parms.dereverb);
	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_DEREVERB_DECAY,
			     &spx->parms.dereverb_decay);
	speex_preprocess_ctl(spx->state, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL,
			     &spx->parms.dereverb_level);
	return 0;
}

static int spx_close(snd_pcm_extplug_t *ext)
{
	snd_pcm_speex_t *spx = (snd_pcm_speex_t *)ext;
	free(spx->outbuf);
	free(spx->buf);
	if (spx->state)
		speex_preprocess_state_destroy(spx->state);
	if (spx->echo_state)
		speex_echo_state_destroy(spx->echo_state);	
	return 0;
}

static const snd_pcm_extplug_callback_t speex_callback = {
	.transfer = spx_transfer,
	.init = spx_init,
	.close = spx_close,
};

static int get_bool_parm(snd_config_t *n, const char *id, const char *str,
			 int *val_ret)
{
	int val;
	if (strcmp(id, str))
		return 0;

	val = snd_config_get_bool(n);
	if (val < 0) {
		SNDERR("Invalid value for %s", id);
		return val;
	}
	*val_ret = val;
	return 1;
}

static int get_int_parm(snd_config_t *n, const char *id, const char *str,
			int *val_ret)
{
	long val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_integer(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s parameter", id);
		return err;
	}
	*val_ret = val;
	return 1;
}

static int get_float_parm(snd_config_t *n, const char *id, const char *str,
			  float *val_ret)
{
	double val;
	int err;

	if (strcmp(id, str))
		return 0;
	err = snd_config_get_ireal(n, &val);
	if (err < 0) {
		SNDERR("Invalid value for %s", id);
		return err;
	}
	*val_ret = val;
	return 1;
}

SND_PCM_PLUGIN_DEFINE_FUNC(speex)
{
	snd_config_iterator_t i, next;
	snd_pcm_speex_t *spx;
	snd_config_t *sconf = NULL;
	int err;
	struct spx_parms parms = {
		.frames = 64,
		.denoise = 1,
		.agc = 0,
		.agc_level = 8000,
		.dereverb = 0,
		.dereverb_decay = 0,
		.dereverb_level = 0,
		.echo = 0,
		.filter_length = 256,
	};

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 ||
		    strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			sconf = n;
			continue;
		}
		err = get_int_parm(n, id, "frames", &parms.frames);
		if (err)
			goto ok;
		err = get_bool_parm(n, id, "denoise", &parms.denoise);
		if (err)
			goto ok;
		err = get_bool_parm(n, id, "agc", &parms.agc);
		if (err)
			goto ok;
		err = get_float_parm(n, id, "agc_level", &parms.agc_level);
		if (err)
			goto ok;
		err = get_bool_parm(n, id, "dereverb", &parms.dereverb);
		if (err)
			goto ok;
		err = get_float_parm(n, id, "dereverb_decay",
				     &parms.dereverb_decay);
		if (err)
			goto ok;
		err = get_float_parm(n, id, "dereverb_level",
				     &parms.dereverb_level);
		if (err)
			goto ok;
		err = get_bool_parm(n, id, "echo", &parms.echo);
              	if (err)
                	goto ok;
		err = get_int_parm(n, id, "filter_length",
				   &parms.filter_length);
            	if (err)
			goto ok;	
		SNDERR("Unknown field %s", id);
		err = -EINVAL;
	ok:
		if (err < 0)
			return err;
	}

	if (!sconf) {
		SNDERR("No slave configuration for speex pcm");
		return -EINVAL;
	}

	spx = calloc(1, sizeof(*spx));
	if (!spx)
		return -ENOMEM;

	spx->ext.version = SND_PCM_EXTPLUG_VERSION;
	spx->ext.name = "Speex DSP Plugin";
	spx->ext.callback = &speex_callback;
	spx->ext.private_data = spx;
	spx->parms = parms;

	err = snd_pcm_extplug_create(&spx->ext, name, root, sconf,
				     stream, mode);
	if (err < 0) {
		free(spx);
		return err;
	}

	snd_pcm_extplug_set_param(&spx->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 1);
	snd_pcm_extplug_set_slave_param(&spx->ext,
					SND_PCM_EXTPLUG_HW_CHANNELS, 1);
	snd_pcm_extplug_set_param(&spx->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&spx->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S16);

	*pcmp = spx->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(speex);
