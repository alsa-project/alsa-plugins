/*-*- linux-c -*-*/

/*
 * ALSA configuration function extensions for pulse
 *
 * Copyright (c) 2008 by Sjoerd Simons <sjoerd@luon.net>
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 *
 */

#include <stdio.h>

#include <alsa/asoundlib.h>
#include <pulse/pulseaudio.h>


/* Not actually part of the alsa api....  */
extern int
snd_config_hook_load(snd_config_t * root, snd_config_t * config,
		     snd_config_t ** dst, snd_config_t * private_data);

int
conf_pulse_hook_load_if_running(snd_config_t * root, snd_config_t * config,
				snd_config_t ** dst,
				snd_config_t * private_data)
{
	pa_mainloop *loop = NULL;
	pa_context *context = NULL;
	int ret = 0, err, state;

	*dst = NULL;

	/* Defined if we're called inside the pulsedaemon itself */
	if (getenv("PULSE_INTERNAL") != NULL)
		goto out;

	loop = pa_mainloop_new();
	if (loop == NULL)
		goto out;

	context = pa_context_new(pa_mainloop_get_api(loop), "Alsa hook");
	if (context == NULL)
		goto out;

	err = pa_context_connect(context, NULL, 0, NULL);
	if (err < 0)
		goto out;

	do {
		err = pa_mainloop_iterate(loop, 1, NULL);
		if (err < 0)
			goto out;

		state = pa_context_get_state(context);
	} while (state < PA_CONTEXT_AUTHORIZING);

	if (state > PA_CONTEXT_READY)
		goto out;

	ret = snd_config_hook_load(root, config, dst, private_data);

      out:
	if (context != NULL)
		pa_context_unref(context);

	if (loop != NULL)
		pa_mainloop_free(loop);

	return ret;
}

SND_DLSYM_BUILD_VERSION(conf_pulse_hook_load_if_running,
			SND_CONFIG_DLSYM_VERSION_HOOK);
