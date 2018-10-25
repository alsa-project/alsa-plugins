/*
 * AVTP Audio Format (AAF) PCM Plugin
 *
 * Copyright (c) 2018, Intel Corporation
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

typedef struct {
	snd_pcm_ioplug_t io;
} snd_pcm_aaf_t;

static int aaf_close(snd_pcm_ioplug_t *io)
{
	free(io->private_data);
	return 0;
}

static snd_pcm_sframes_t aaf_pointer(snd_pcm_ioplug_t *io)
{
	return 0;
}

static int aaf_start(snd_pcm_ioplug_t *io)
{
	return 0;
}

static int aaf_stop(snd_pcm_ioplug_t *io)
{
	return 0;
}

static const snd_pcm_ioplug_callback_t aaf_callback = {
	.close = aaf_close,
	.pointer = aaf_pointer,
	.start = aaf_start,
	.stop = aaf_stop,
};

SND_PCM_PLUGIN_DEFINE_FUNC(aaf)
{
	snd_pcm_aaf_t *aaf;
	int res;

	aaf = calloc(1, sizeof(*aaf));
	if (!aaf) {
		SNDERR("Failed to allocate memory");
		return -ENOMEM;
	}

	aaf->io.version = SND_PCM_IOPLUG_VERSION;
	aaf->io.name = "AVTP Audio Format (AAF) Plugin";
	aaf->io.callback = &aaf_callback;
	aaf->io.private_data = aaf;
	res = snd_pcm_ioplug_create(&aaf->io, name, stream, mode);
	if (res < 0) {
		SNDERR("Failed to create ioplug instance");
		goto err;
	}

	*pcmp = aaf->io.pcm;
	return 0;

err:
	free(aaf);
	return res;
}

SND_PCM_PLUGIN_SYMBOL(aaf);
