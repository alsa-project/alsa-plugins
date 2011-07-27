/*-*- linux-c -*-*/

/*
 * ALSA <-> PulseAudio plugins
 *
 * Copyright (c) 2006 by Pierre Ossman <ossman@cendio.se>
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

#include <pulse/pulseaudio.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

typedef struct snd_pulse {
	pa_threaded_mainloop *mainloop;
	pa_context *context;

	int thread_fd, main_fd;
} snd_pulse_t;

int pulse_check_connection(snd_pulse_t * p);

void pulse_context_success_cb(pa_context * c, int success, void *userdata);

int pulse_wait_operation(snd_pulse_t * p, pa_operation * o);

snd_pulse_t *pulse_new(void);
void pulse_free(snd_pulse_t * p);

int pulse_connect(snd_pulse_t * p, const char *server, int can_fallback);

void pulse_poll_activate(snd_pulse_t * p);
void pulse_poll_deactivate(snd_pulse_t * p);
