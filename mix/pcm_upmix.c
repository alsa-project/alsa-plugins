/*
 * Automatic upmix plugin
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

typedef struct snd_pcm_upmix snd_pcm_upmix_t;

typedef void (*upmixer_t)(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size);

struct snd_pcm_upmix {
	snd_pcm_extplug_t ext;
	/* setup */
	int delay_ms;
	/* privates */
	upmixer_t upmix;
	unsigned int curpos;
	int delay;
	short *delayline[2];
};

static inline void *area_addr(const snd_pcm_channel_area_t *area,
			      snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

static inline unsigned int area_step(const snd_pcm_channel_area_t *area)
{
	return area->step / 8;
}

/* Delayed copy SL & SR */
static void delayed_copy(snd_pcm_upmix_t *mix,
			 const snd_pcm_channel_area_t *dst_areas,
			 snd_pcm_uframes_t dst_offset,
			 const snd_pcm_channel_area_t *src_areas,
			 snd_pcm_uframes_t src_offset,
			 unsigned int size)
{
	unsigned int i, p, delay, curpos, dst_step, src_step;
	short *dst, *src;

	if (! mix->delay_ms) {
		snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
				   2, size, SND_PCM_FORMAT_S16);
		return;
	}

	delay = mix->delay;
	if (delay > size)
		delay = size;
	for (i = 0; i < 2; i++) {
		dst = (short *)area_addr(dst_areas + i, dst_offset);
		dst_step = area_step(dst_areas + i) / 2;
		curpos = mix->curpos;
		for (p = 0; p < delay; p++) {
			*dst = mix->delayline[i][curpos];
			dst += dst_step;
			curpos = (curpos + 1) % mix->delay;
		}
		snd_pcm_area_copy(dst_areas + i, dst_offset + delay,
				  src_areas + i, src_offset,
				  size - delay, SND_PCM_FORMAT_S16);
		src = (short *)area_addr(src_areas + i,
					 src_offset + size - delay);
		src_step = area_step(src_areas + i) / 2;
		curpos = mix->curpos;
		for (p = 0; p < delay; p++) {
			mix->delayline[i][curpos] = *src;
			src += src_step;
			curpos = (curpos + 1) % mix->delay;
		}
	}
	mix->curpos = curpos;
}

/* Average of L+R -> C and LFE */
static void average_copy(const snd_pcm_channel_area_t *dst_areas,
			 snd_pcm_uframes_t dst_offset,
			 const snd_pcm_channel_area_t *src_areas,
			 snd_pcm_uframes_t src_offset,
			 unsigned int nchns,
			 unsigned int size)
{
	short *dst[2], *src[2];
	unsigned int i, dst_step[2], src_step[2];

	for (i = 0; i < nchns; i++) {
		dst[i] = (short *)area_addr(dst_areas + i, dst_offset);
		dst_step[i] = area_step(dst_areas + i) / 2;
	}
	for (i = 0; i < 2; i++) {
		src[i] = (short *)area_addr(src_areas + i, src_offset);
		src_step[i] = area_step(src_areas + i) / 2;
	}
	while (size--) {
		short val = (*src[0] >> 1) + (*src[1] >> 1);
		for (i = 0; i < nchns; i++) {
			*dst[i] = val;
			dst[i] += dst_step[i];
		}
		src[0] += src_step[0];
		src[1] += src_step[1];
	}
}

static void upmix_1_to_71(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	int i;
	for (i = 0; i < 8; i++)
		snd_pcm_area_copy(dst_areas + i, dst_offset,
				  src_areas, src_offset,
				  size, SND_PCM_FORMAT_S16);
}

static void upmix_1_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	int i;
	for (i = 0; i < 6; i++)
		snd_pcm_area_copy(dst_areas + i, dst_offset,
				  src_areas, src_offset,
				  size, SND_PCM_FORMAT_S16);
}

static void upmix_1_to_40(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	int i;
	for (i = 0; i < 4; i++)
		snd_pcm_area_copy(dst_areas + i, dst_offset,
				  src_areas, src_offset,
				  size, SND_PCM_FORMAT_S16);
}

static void upmix_2_to_71(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset,
		     size);
	average_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
		     2, size);
	snd_pcm_areas_copy(dst_areas + 6, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	
}

static void upmix_2_to_51(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset,
		     size);
	average_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
		     2, size);
}

static void upmix_2_to_40(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset,
		     size);
}

static void upmix_3_to_51(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset,
		     size);
	snd_pcm_areas_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
}

static void upmix_3_to_40(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset,
		     size);
}

static void upmix_4_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   4, size, SND_PCM_FORMAT_S16);
	snd_pcm_areas_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
			   2, size, SND_PCM_FORMAT_S16);
}

static void upmix_4_to_40(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   4, size, SND_PCM_FORMAT_S16);
}

static void upmix_5_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   5, size, SND_PCM_FORMAT_S16);
	snd_pcm_area_copy(dst_areas + 5, dst_offset, src_areas + 4, src_offset,
			  size, SND_PCM_FORMAT_S16);
}

static void upmix_6_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   6, size, SND_PCM_FORMAT_S16);
}

static void upmix_8_to_71(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   8, size, SND_PCM_FORMAT_S16);
}

static const upmixer_t do_upmix[8][3] = {
	{ upmix_1_to_40, upmix_1_to_51, upmix_1_to_71 },
	{ upmix_2_to_40, upmix_2_to_51, upmix_2_to_71 },
	{ upmix_3_to_40, upmix_3_to_51, upmix_3_to_51 },
	{ upmix_4_to_40, upmix_4_to_51, upmix_4_to_51 },
	{ upmix_4_to_40, upmix_5_to_51, upmix_5_to_51 },
	{ upmix_4_to_40, upmix_6_to_51, upmix_6_to_51 },
	{ upmix_4_to_40, upmix_6_to_51, upmix_6_to_51 },
	{ upmix_4_to_40, upmix_6_to_51, upmix_8_to_71 },
};

static snd_pcm_sframes_t
upmix_transfer(snd_pcm_extplug_t *ext,
	       const snd_pcm_channel_area_t *dst_areas,
	       snd_pcm_uframes_t dst_offset,
	       const snd_pcm_channel_area_t *src_areas,
	       snd_pcm_uframes_t src_offset,
	       snd_pcm_uframes_t size)
{
	snd_pcm_upmix_t *mix = (snd_pcm_upmix_t *)ext;
	mix->upmix(mix, dst_areas, dst_offset,
		   src_areas, src_offset, size);
	return size;
}

static int upmix_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_upmix_t *mix = (snd_pcm_upmix_t *)ext;
	int ctype, stype;

	switch (ext->slave_channels) {
		case	6:
			stype = 1;
			break;
		case 8:
			stype = 2;
			break;
		default:
			stype = 0;
	}
	ctype = ext->channels - 1;
	if (ctype < 0 || ctype > 7) {
		SNDERR("Invalid channel numbers for upmix: %d", ctype + 1);
		return -EINVAL;
	}
	mix->upmix = do_upmix[ctype][stype];

	if (mix->delay_ms) {
		free(mix->delayline[0]);
		free(mix->delayline[1]);
		mix->delay = ext->rate * mix->delay_ms / 1000;
		mix->delayline[0] = calloc(2, mix->delay);
		mix->delayline[1] = calloc(2, mix->delay);
		if (! mix->delayline[0] || ! mix->delayline[1])
			return -ENOMEM;
		mix->curpos = 0;
	}
	return 0;
}

static int upmix_close(snd_pcm_extplug_t *ext)
{
	snd_pcm_upmix_t *mix = (snd_pcm_upmix_t *)ext;
	free(mix->delayline[0]);
	free(mix->delayline[1]);
	return 0;
}

static const snd_pcm_extplug_callback_t upmix_callback = {
	.transfer = upmix_transfer,
	.init = upmix_init,
	.close = upmix_close,
};

SND_PCM_PLUGIN_DEFINE_FUNC(upmix)
{
	snd_config_iterator_t i, next;
	snd_pcm_upmix_t *mix;
	snd_config_t *sconf = NULL;
	static const unsigned int chlist[3] = {4, 6, 8};
	unsigned int channels = 0;
	int delay = 10;
	int err;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			sconf = n;
			continue;
		}
		if (strcmp(id, "delay") == 0) {
			long val;
			err = snd_config_get_integer(n, &val);
			if (err < 0) {
				SNDERR("Invalid value for %s", id);
				return err;
			}
			delay = val;
			continue;
		}
		if (strcmp(id, "channels") == 0) {
			long val;
			err = snd_config_get_integer(n, &val);
			if (err < 0) {
				SNDERR("Invalid value for %s", id);
				return err;
			}
			channels = val;
			if (channels != 4 && channels != 6 && channels != 0 && channels != 8) {
				SNDERR("channels must be 4, 6, 8 or 0");
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (! sconf) {
		SNDERR("No slave configuration for filrmix pcm");
		return -EINVAL;
	}

	mix = calloc(1, sizeof(*mix));
	if (mix == NULL)
		return -ENOMEM;

	mix->ext.version = SND_PCM_EXTPLUG_VERSION;
	mix->ext.name = "Upmix Plugin";
	mix->ext.callback = &upmix_callback;
	mix->ext.private_data = mix;
	if (delay < 0)
		delay = 0;
	else if (delay > 1000)
		delay = 1000;
	mix->delay_ms = delay;

	err = snd_pcm_extplug_create(&mix->ext, name, root, sconf, stream, mode);
	if (err < 0) {
		free(mix);
		return err;
	}

	snd_pcm_extplug_set_param_minmax(&mix->ext,
					 SND_PCM_EXTPLUG_HW_CHANNELS,
					 1, 8);
	if (channels)
		snd_pcm_extplug_set_slave_param_minmax(&mix->ext,
						       SND_PCM_EXTPLUG_HW_CHANNELS,
						       channels, channels);
	else
		snd_pcm_extplug_set_slave_param_list(&mix->ext,
						     SND_PCM_EXTPLUG_HW_CHANNELS,
						     3, chlist);
	snd_pcm_extplug_set_param(&mix->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&mix->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S16);

	*pcmp = mix->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(upmix);
