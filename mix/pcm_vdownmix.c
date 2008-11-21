/*
 * 4/5/6 -> 2 downmix with a simple spacialization
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

/* #define I_AM_POWERFUL */

#ifdef I_AM_POWERFUL
#define RINGBUF_SIZE	(1 << 9)
#else
#define RINGBUF_SIZE	(1 << 7)
#endif
#define RINGBUF_MASK	(RINGBUF_SIZE - 1)

struct vdownmix_tap {
	int delay;
	int weight;
};

#define MAX_TAPS	30

struct vdownmix_filter {
	int taps;
	struct vdownmix_tap tap[MAX_TAPS];
};

typedef struct {
	snd_pcm_extplug_t ext;
	int channels;
	unsigned int curpos;
	short rbuf[RINGBUF_SIZE][5];
} snd_pcm_vdownmix_t;

static const struct vdownmix_filter tap_filters[5] = {
	{
#ifdef I_AM_POWERFUL
		18,
#else
		14,
#endif
		{{ 0, 0xfffffd0a },
		 { 1, 0x41d },
		 { 2, 0xffffe657 },
		 { 3, 0x6eb5 },
		 { 4, 0xffffe657 },
		 { 5, 0x41d },
		 { 6, 0xfffffd0a },
		 { 71, 0xffffff1c },
		 { 72, 0x12e },
		 { 73, 0xfffff81a },
		 { 74, 0x24de },
		 { 75, 0xfffff81a },
		 { 76, 0x12e },
		 { 77, 0xffffff1c },
		 { 265, 0xfffffc65 },
		 { 266, 0xee1 },
		 { 267, 0xfffffc65 },
		 { 395, 0x46a }},
	},

	{
#ifdef I_AM_POWERFUL
		17,
#else
		10,
#endif
		{{ 8, 0xcf },
		 { 9, 0xa7b },
		 { 10, 0xcd7 },
		 { 11, 0x5b3 },
		 { 12, 0x859 },
		 { 13, 0xaf },
		 { 80, 0x38b },
		 { 81, 0x454 },
		 { 82, 0x218 },
		 { 83, 0x2c1 },
		 { 268, 0x58b },
		 { 275, 0xc2 },
		 { 397, 0xbd },
		 { 398, 0x1e8 },
		 { 506, 0xfffffeac },
		 { 507, 0x636 },
		 { 508, 0xfffffeac }},
	},

	{
#ifdef I_AM_POWERFUL
		11,
#else
		1,
#endif
		{{ 3, 0x4000 },
		 { 125, 0x12a },
		 { 126, 0xda1 },
		 { 127, 0x12a },
		 { 193, 0xfffffed3 },
		 { 194, 0xdb9 },
		 { 195, 0xfffffed3 },
		 { 454, 0x10a },
		 { 483, 0xfffffe97 },
		 { 484, 0x698 },
		 { 485, 0xfffffe97 }},
	},

	{
#ifdef I_AM_POWERFUL
		25,
#else
		10,
#endif
		{{ 5, 0x1cb },
		 { 6, 0x9c5 },
		 { 7, 0x117e },
		 { 8, 0x200 },
		 { 9, 0x533 },
		 { 10, 0x1c6 },
		 { 11, 0x167 },
		 { 12, 0x5ff },
		 { 13, 0x425 },
		 { 14, 0xd9 },
		 { 128, 0x247 },
		 { 129, 0x5e1 },
		 { 130, 0xb7 },
		 { 131, 0x122 },
		 { 135, 0x10a },
		 { 200, 0x1b6 },
		 { 201, 0xa7 },
		 { 202, 0x188 },
		 { 203, 0x1d9 },
		 { 445, 0xffffff44 },
		 { 446, 0x5e2 },
		 { 447, 0xffffff44 },
		 { 484, 0xffffff51 },
		 { 485, 0x449 },
		 { 486, 0xffffff51 }},
	},

	{
#ifdef I_AM_POWERFUL
		21,
#else
		7,
#endif
		{{ 0, 0xfffffdee },
		 { 1, 0x28b },
		 { 2, 0xffffed1e },
		 { 3, 0x6336 },
		 { 4, 0xffffed1e },
		 { 5, 0x28b },
		 { 6, 0xfffffdee },
		 { 51, 0xffffff2c },
		 { 52, 0x105 },
		 { 53, 0xfffff86b },
		 { 54, 0x27d9 },
		 { 55, 0xfffff86b },
		 { 56, 0x105 },
		 { 57, 0xffffff2c },
		 { 333, 0xfffffd69 },
		 { 334, 0xb2f },
		 { 335, 0xfffffd69 },
		 { 339, 0xdf },
		 { 340, 0x168 },
		 { 342, 0xa6 },
		 { 343, 0xba }},
	},
};

static const int tap_index[5][2] = {
	/* left */
	{ 0, 1 },
	/* right */
	{ 1, 0 },
	/* rear left */
	{ 2, 3 },
	/* rear right */
	{ 3, 2 },
	/* center */
	{ 4, 4 },
};

static inline void *area_addr(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

static inline unsigned int area_step(const snd_pcm_channel_area_t *area)
{
	return area->step / 8;
}

static snd_pcm_sframes_t
vdownmix_transfer(snd_pcm_extplug_t *ext,
		  const snd_pcm_channel_area_t *dst_areas,
		  snd_pcm_uframes_t dst_offset,
		  const snd_pcm_channel_area_t *src_areas,
		  snd_pcm_uframes_t src_offset,
		  snd_pcm_uframes_t size)
{
	snd_pcm_vdownmix_t *mix = (snd_pcm_vdownmix_t *)ext;
	short *src[mix->channels], *ptr[2];
	unsigned int src_step[mix->channels], step[2];
	int i, ch, curpos, p, idx;
	int acc[2];
	int fr;

	ptr[0] = area_addr(dst_areas, dst_offset);
	step[0] = area_step(dst_areas) / 2;
	ptr[1] = area_addr(dst_areas + 1, dst_offset);
	step[1] = area_step(dst_areas + 1) / 2;
	for (ch = 0; ch < mix->channels; ch++) {
		const snd_pcm_channel_area_t *src_area = &src_areas[ch];
		src[ch] = area_addr(src_area, src_offset);
		src_step[ch] = area_step(src_area) / 2;
	}
	curpos = mix->curpos;
	fr = size;
	while (fr--) {
		acc[0] = acc[1] = 0;
		for (ch = 0; ch < mix->channels; ch++) {
			mix->rbuf[curpos][ch] = *src[ch];
			for (idx = 0; idx < 2; idx++) {
				int f = tap_index[ch][idx];
				const struct vdownmix_filter *filter;
				filter = &tap_filters[f];
				for (i = 0; i < filter->taps; i++) {
					p = (curpos + RINGBUF_SIZE - filter->tap[i].delay)
						& RINGBUF_MASK;
					acc[idx] += mix->rbuf[p][ch] * filter->tap[i].weight;
				}
			}
			src[ch] += src_step[ch];
		}
		for (idx = 0; idx < 2; idx++) {
			acc[idx] >>= 14;
			if (acc[idx] < -32768)
				*ptr[idx] = -32768;
			else if (acc[idx] > 32767)
				*ptr[idx] = 32767;
			else
				*ptr[idx] = acc[idx];
			ptr[idx] += step[idx];
		}
		curpos = (curpos + 1) & RINGBUF_MASK;
	}
	mix->curpos = curpos;
	return size;
}


static int vdownmix_init(snd_pcm_extplug_t *ext)
{
	snd_pcm_vdownmix_t *mix = (snd_pcm_vdownmix_t *)ext;
	mix->channels = ext->channels;
	if (mix->channels > 5) /* ignore LFE */
		mix->channels = 5;
	mix->curpos = 0;
	memset(mix->rbuf, 0, sizeof(mix->rbuf));
	return 0;
}

static const snd_pcm_extplug_callback_t vdownmix_callback = {
	.transfer = vdownmix_transfer,
	.init = vdownmix_init,
	/* .dump = filr_dump, */
};

SND_PCM_PLUGIN_DEFINE_FUNC(vdownmix)
{
	snd_config_iterator_t i, next;
	snd_pcm_vdownmix_t *mix;
	snd_config_t *sconf = NULL;
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
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (! sconf) {
		SNDERR("No slave configuration for vdownmix pcm");
		return -EINVAL;
	}

	mix = calloc(1, sizeof(*mix));
	if (mix == NULL)
		return -ENOMEM;

	mix->ext.version = SND_PCM_EXTPLUG_VERSION;
	mix->ext.name = "Vdownmix Plugin";
	mix->ext.callback = &vdownmix_callback;
	mix->ext.private_data = mix;

	err = snd_pcm_extplug_create(&mix->ext, name, root, sconf, stream, mode);
	if (err < 0) {
		free(mix);
		return err;
	}

	/* 4/5/6 -> 2 downmix */
	snd_pcm_extplug_set_param_minmax(&mix->ext, SND_PCM_EXTPLUG_HW_CHANNELS,
					 4, 6);
	snd_pcm_extplug_set_slave_param(&mix->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 2);
	snd_pcm_extplug_set_param(&mix->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&mix->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S16);

	*pcmp = mix->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(vdownmix);
