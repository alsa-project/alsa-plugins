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

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/poll.h>

#include "polyp.h"

int polyp_check_connection(snd_polyp_t *p)
{
    pa_context_state_t state;

    assert(p && p->context && p->mainloop);

    state = pa_context_get_state(p->context);

    if (state != PA_CONTEXT_READY)
        return -EIO;

    return 0;
}

void polyp_stream_state_cb(pa_stream *s, void * userdata)
{
    snd_polyp_t *p = userdata;

    assert(s);
    assert(p);

    pa_threaded_mainloop_signal(p->mainloop, 0);
}

void polyp_stream_success_cb(pa_stream *s, int success, void *userdata)
{
    snd_polyp_t *p = userdata;

    assert(s);
    assert(p);

    pa_threaded_mainloop_signal(p->mainloop, 0);
}

void polyp_context_success_cb(pa_context *c, int success, void *userdata)
{
    snd_polyp_t *p = userdata;

    assert(c);
    assert(p);

    pa_threaded_mainloop_signal(p->mainloop, 0);
}

int polyp_wait_operation(snd_polyp_t *p, pa_operation *o)
{
    assert(p && o && (p->state == POLYP_STATE_READY) && p->mainloop);

    while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(p->mainloop);

    return 0;
}

int polyp_wait_stream_state(snd_polyp_t *p, pa_stream *stream, pa_stream_state_t target)
{
    pa_stream_state_t state;

    assert(p && stream && (p->state == POLYP_STATE_READY) && p->mainloop);

    while (1) {
        state = pa_stream_get_state(stream);

        if (state == PA_STREAM_FAILED)
            return -EIO;

        if (state == target)
            break;

        pa_threaded_mainloop_wait(p->mainloop);
    }

    return 0;
}

static void context_state_cb(pa_context *c, void *userdata) {
    snd_polyp_t *p = userdata;
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            pa_threaded_mainloop_signal(p->mainloop, 0);
            break;

        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
    }
}

snd_polyp_t *polyp_new()
{
    snd_polyp_t *p;
	int fd[2] = { -1, -1 };
	char proc[PATH_MAX], buf[PATH_MAX + 20];

    p = calloc(1, sizeof(snd_polyp_t));
    assert(p);

    p->state = POLYP_STATE_INIT;

    if (pipe(fd)) {
        free(p);
        return NULL;
    }

    p->main_fd = fd[0];
    p->thread_fd = fd[1];

    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    fcntl(fd[1], F_SETFL, O_NONBLOCK);

    signal(SIGPIPE, SIG_IGN); /* Yes, ugly as hell */

    p->mainloop = pa_threaded_mainloop_new();
    assert(p->mainloop);

    if (pa_threaded_mainloop_start(p->mainloop) < 0) {
        pa_threaded_mainloop_free(p->mainloop);
        close(fd[0]);
        close(fd[1]);
        free(p);
        return NULL;
    }

    if (pa_get_binary_name(proc, sizeof(proc)))
        snprintf(buf, sizeof(buf), "ALSA plug-in [%s]", pa_path_get_filename(proc));
    else
        snprintf(buf, sizeof(buf), "ALSA plug-in");

    p->context = pa_context_new(pa_threaded_mainloop_get_api(p->mainloop), buf);
    assert(p->context);

    return p;
}

void polyp_free(snd_polyp_t *p)
{
    pa_threaded_mainloop_stop(p->mainloop);

    pa_context_unref(p->context);
    pa_threaded_mainloop_free(p->mainloop);

    close(p->thread_fd);
    close(p->main_fd);

    free(p);
}

int polyp_connect(snd_polyp_t *p, const char *server)
{
    int err;

    assert(p && p->context && p->mainloop && (p->state == POLYP_STATE_INIT));

    pa_threaded_mainloop_lock(p->mainloop);

    err = pa_context_connect(p->context, server, 0, NULL);
    if (err < 0)
        goto error;

    pa_context_set_state_callback(p->context, context_state_cb, p);

    pa_threaded_mainloop_wait(p->mainloop);

    if (pa_context_get_state(p->context) != PA_CONTEXT_READY)
        goto error;

    pa_threaded_mainloop_unlock(p->mainloop);

    p->state = POLYP_STATE_READY;

    return 0;

error:
    fprintf(stderr, "*** POLYPAUDIO: Unable to connect: %s\n",
        pa_strerror(pa_context_errno(p->context)));

    pa_threaded_mainloop_unlock(p->mainloop);

    return -ECONNREFUSED;
}

void polyp_poll_activate(snd_polyp_t *p)
{
    assert(p);

    write(p->thread_fd, "a", 1);
}

void polyp_poll_deactivate(snd_polyp_t *p)
{
	char buf[10];

	assert(p);

    /* Drain the pipe */
    while (read(p->main_fd, buf, sizeof(buf)) > 0);
}

int polyp_poll_descriptors_count(snd_polyp_t *p)
{
    assert(p);

    if (p->main_fd >= 0)
        return 1;
    else
        return 0;
}

int polyp_poll_descriptors(snd_polyp_t *p, struct pollfd *pfd, unsigned int space)
{
    assert(p);

    assert(space >= 1);

    pfd[0].fd = p->main_fd;
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;

    return 1;
}

int polyp_poll_revents(snd_polyp_t *p, struct pollfd *pfd, unsigned int nfds, unsigned short *revents)
{
    assert(p);

    return 1;
}
