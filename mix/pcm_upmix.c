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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#define UPMIX_PCM_FORMAT	SND_PCM_FORMAT_S16

typedef short upmix_sample_t;

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
	upmix_sample_t *delayline[2];
};

/* Get the current address of a channel area */
static inline void *area_addr(const snd_pcm_channel_area_t *area,
			      snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

/* Convert step size in bits to steps of samples */
static inline unsigned int area_step(const snd_pcm_channel_area_t *area)
{
	return area->step / 8 / sizeof(upmix_sample_t);
}

/* Delayed copy SL & SR */
static void delayed_copy(snd_pcm_upmix_t *mix,
			 const snd_pcm_channel_area_t *dst_areas,
			 snd_pcm_uframes_t dst_offset,
			 const snd_pcm_channel_area_t *src_areas,
			 snd_pcm_uframes_t src_offset,
			 unsigned int size)
{
	unsigned int channel, p, delay, curpos, dst_step, src_step;
	upmix_sample_t *dst, *src;

	if (! mix->delay_ms) {
		snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
				   2, size, UPMIX_PCM_FORMAT);
		return;
	}

	delay = mix->delay;
	if (delay > size)
		delay = size;

	for (channel = 0; channel < 2; channel++) {
		dst = (upmix_sample_t *)area_addr(&dst_areas[channel], dst_offset);
		dst_step = area_step(&dst_areas[channel]);
		curpos = mix->curpos;
		for (p = 0; p < delay; p++) {
			*dst = mix->delayline[channel][curpos];
			dst += dst_step;
			curpos = (curpos + 1) % mix->delay;
		}
		snd_pcm_area_copy(&dst_areas[channel], dst_offset + delay,
				  &src_areas[channel], src_offset,
				  size - delay, UPMIX_PCM_FORMAT);
		src = (upmix_sample_t *)area_addr(&src_areas[channel],
						  src_offset + size - delay);
		src_step = area_step(&src_areas[channel]);
		curpos = mix->curpos;
		for (p = 0; p < delay; p++) {
			mix->delayline[channel][curpos] = *src;
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
			 unsigned int size)
{
	static const unsigned int nchns = 2;
	upmix_sample_t *dst[nchns], *src[2];
	unsigned int channel, dst_step[nchns], src_step[2];

	for (channel = 0; channel < nchns; channel++) {
		dst[channel] = (upmix_sample_t *)area_addr(&dst_areas[channel], dst_offset);
		dst_step[channel] = area_step(&dst_areas[channel]);
	}
	for (channel = 0; channel < 2; channel++) {
		src[channel] = (upmix_sample_t *)area_addr(&src_areas[channel], src_offset);
		src_step[channel] = area_step(&src_areas[channel]);
	}
	while (size--) {
		upmix_sample_t val = (*src[0] >> 1) + (*src[1] >> 1);
		for (channel = 0; channel < nchns; channel++) {
			*dst[channel] = val;
			dst[channel] += dst_step[channel];
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
	int channel;
	for (channel = 0; channel < 8; channel++)
		snd_pcm_area_copy(&dst_areas[channel], dst_offset,
				  src_areas, src_offset,
				  size, UPMIX_PCM_FORMAT);
}

static void upmix_1_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	int channel;
	for (channel = 0; channel < 6; channel++)
		snd_pcm_area_copy(&dst_areas[channel], dst_offset,
				  src_areas, src_offset,
				  size, UPMIX_PCM_FORMAT);
}

static void upmix_1_to_40(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	int channel;
	for (channel = 0; channel < 4; channel++)
		snd_pcm_area_copy(&dst_areas[channel], dst_offset,
				  src_areas, src_offset,
				  size, UPMIX_PCM_FORMAT);
}

static void upmix_2_to_71(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset, size);
	average_copy(dst_areas + 4, dst_offset, src_areas, src_offset, size);
	snd_pcm_areas_copy(dst_areas + 6, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	
}

static void upmix_2_to_51(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset, size);
	average_copy(dst_areas + 4, dst_offset, src_areas, src_offset, size);
}

static void upmix_2_to_40(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset, size);
}

static void upmix_3_to_51(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset, size);
	snd_pcm_areas_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
}

static void upmix_3_to_40(snd_pcm_upmix_t *mix,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
	delayed_copy(mix, dst_areas + 2, dst_offset, src_areas, src_offset, size);
}

static void upmix_4_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   4, size, UPMIX_PCM_FORMAT);
	snd_pcm_areas_copy(dst_areas + 4, dst_offset, src_areas, src_offset,
			   2, size, UPMIX_PCM_FORMAT);
}

static void upmix_4_to_40(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   4, size, UPMIX_PCM_FORMAT);
}

static void upmix_5_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   5, size, UPMIX_PCM_FORMAT);
	snd_pcm_area_copy(dst_areas + 5, dst_offset, src_areas + 4, src_offset,
			  size, UPMIX_PCM_FORMAT);
}

static void upmix_6_to_51(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   6, size, UPMIX_PCM_FORMAT);
}

static void upmix_8_to_71(snd_pcm_upmix_t *mix ATTRIBUTE_UNUSED,
			  const snd_pcm_channel_area_t *dst_areas,
			  snd_pcm_uframes_t dst_offset,
			  const snd_pcm_channel_area_t *src_areas,
			  snd_pcm_uframes_t src_offset,
			  snd_pcm_uframes_t size)
{
	snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset,
			   8, size, UPMIX_PCM_FORMAT);
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
		case 6:
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
		mix->delayline[0] = calloc(sizeof(upmix_sample_t), mix->delay);
		mix->delayline[1] = calloc(sizeof(upmix_sample_t), mix->delay);
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

#if SND_PCM_EXTPLUG_VERSION >= 0x10002
static unsigned int chmap[8][8] = {
	{ SND_CHMAP_MONO },
	{ SND_CHMAP_FL, SND_CHMAP_FR },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_FC },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR, SND_CHMAP_FC },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR, SND_CHMAP_FC, SND_CHMAP_LFE },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR, SND_CHMAP_FC, SND_CHMAP_LFE, SND_CHMAP_UNKNOWN },
	{ SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR, SND_CHMAP_FC, SND_CHMAP_LFE, SND_CHMAP_SL, SND_CHMAP_SR },
};

static snd_pcm_chmap_query_t **upmix_query_chmaps(snd_pcm_extplug_t *ext ATTRIBUTE_UNUSED)
{
	snd_pcm_chmap_query_t **maps;
	int i;

	maps = calloc(9, sizeof(void *));
	if (!maps)
		return NULL;
	for (i = 0; i < 8; i++) {
		snd_pcm_chmap_query_t *p;
		p = maps[i] = calloc(i + 1 + 2, sizeof(int));
		if (!p) {
			snd_pcm_free_chmaps(maps);
			return NULL;
		}
		p->type = SND_CHMAP_TYPE_FIXED;
		p->map.channels = i + 1;
		memcpy(p->map.pos, &chmap[i][0], (i + 1) * sizeof(int));
	}
	return maps;
}

static snd_pcm_chmap_t *upmix_get_chmap(snd_pcm_extplug_t *ext)
{
	snd_pcm_chmap_t *map;

	if (ext->channels < 1 || ext->channels > 8)
		return NULL;
	map = malloc((ext->channels + 1) * sizeof(int));
	if (!map)
		return NULL;
	map->channels = ext->channels;
	memcpy(map->pos, &chmap[ext->channels - 1][0], ext->channels * sizeof(int));
	return map;
}
#endif /* SND_PCM_EXTPLUG_VERSION >= 0x10002 */

static const snd_pcm_extplug_callback_t upmix_callback = {
	.transfer = upmix_transfer,
	.init = upmix_init,
	.close = upmix_close,
#if SND_PCM_EXTPLUG_VERSION >= 0x10002
	.query_chmaps = upmix_query_chmaps,
	.get_chmap = upmix_get_chmap,
#endif
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
				  UPMIX_PCM_FORMAT);
	snd_pcm_extplug_set_slave_param(&mix->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					UPMIX_PCM_FORMAT);

	*pcmp = mix->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(upmix);
