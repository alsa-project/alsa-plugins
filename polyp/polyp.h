/*
 * ALSA <-> Polypaudio plugins
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

#include <polyp/polypaudio.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

typedef struct snd_polyp {
    pa_threaded_mainloop *mainloop;
    pa_context *context;

    int thread_fd, main_fd;

    enum {
        POLYP_STATE_INIT,
        POLYP_STATE_READY,
    } state;
} snd_polyp_t;

int polyp_check_connection(snd_polyp_t *p);

void polyp_stream_state_cb(pa_stream *s, void * userdata);
void polyp_stream_success_cb(pa_stream *s, int success, void *userdata);
void polyp_context_success_cb(pa_context *c, int success, void *userdata);

int polyp_wait_operation(snd_polyp_t *p, pa_operation *o);
int polyp_wait_stream_state(snd_polyp_t *p, pa_stream *stream, pa_stream_state_t target);

snd_polyp_t *polyp_new();
void polyp_free(snd_polyp_t *p);

int polyp_connect(snd_polyp_t *p, const char *server);

void polyp_poll_activate(snd_polyp_t *p);
void polyp_poll_deactivate(snd_polyp_t *p);
int polyp_poll_descriptors_count(snd_polyp_t *p);
int polyp_poll_descriptors(snd_polyp_t *p, struct pollfd *pfd, unsigned int space);
int polyp_poll_revents(snd_polyp_t *p, struct pollfd *pfd, unsigned int nfds, unsigned short *revents);
