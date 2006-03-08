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
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <pthread.h>

#include "polyp.h"

enum {
    COMMAND_POLL = 'p',
    COMMAND_QUIT = 'q',
    COMMAND_POLL_DONE = 'P',
    COMMAND_POLL_FAILED = 'F',
};

static int write_command(snd_polyp_t *p, char command)
{
    if (write(p->main_fd, &command, 1) != 1)
        return -errno;
    return 0;
}

static int write_reply(snd_polyp_t *p, char reply)
{
    if (write(p->thread_fd, &reply, 1) != 1)
        return -errno;
    return 0;
}

static int read_command(snd_polyp_t *p)
{
    char command;

    if (read(p->thread_fd, &command, 1) != 1)
        return -errno;

    return command;
}

static int read_reply(snd_polyp_t *p)
{
    char reply;

    if (read(p->main_fd, &reply, 1) != 1)
        return -errno;

    return reply;
}

static void* thread_func(void *data)
{
    snd_polyp_t *p = (snd_polyp_t*)data;
    sigset_t mask;
    char command;
    int ret;

    sigfillset(&mask);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    do {
        command = read_command(p);
        if (command < 0)
            break;

        switch (command) {
        case COMMAND_POLL:
            do {
                ret = pa_mainloop_poll(p->mainloop);
            } while ((ret < 0) && (errno == EINTR));

            ret = write_reply(p, (ret < 0) ? COMMAND_POLL_FAILED : COMMAND_POLL_DONE);
            if (ret < 0)
                return NULL;

            break;
        }
    } while (command != COMMAND_QUIT);

    return NULL;
}

int polyp_start_poll(snd_polyp_t *p)
{
    int err;

    assert(p);

    if (p->state == POLYP_STATE_POLLING)
        return 0;

    assert(p->state == POLYP_STATE_READY);

    err = pa_mainloop_prepare(p->mainloop, -1);
    if (err < 0)
        return err;

    err = write_command(p, COMMAND_POLL);
    if (err < 0)
        return err;

    p->state = POLYP_STATE_POLLING;

    return 0;
}

int polyp_finish_poll(snd_polyp_t *p)
{
	char reply;
	int err;

    assert(p);

    if (p->state == POLYP_STATE_READY)
        return 0;

    assert(p->state == POLYP_STATE_POLLING);

    p->state = POLYP_STATE_READY;

    pa_mainloop_wakeup(p->mainloop);

    reply = read_reply(p);

    if (reply == COMMAND_POLL_DONE) {
        err = pa_mainloop_dispatch(p->mainloop);
        if (err < 0)
            return err;
    } else
        return -EIO;

    return 0;
}

int polyp_check_connection(snd_polyp_t *p)
{
    pa_context_state_t state;

    assert(p && p->context);

    state = pa_context_get_state(p->context);

    if (state != PA_CONTEXT_READY)
        return -EIO;

    return 0;
}

int polyp_wait_operation(snd_polyp_t *p, pa_operation *o)
{
    int err;

    assert(p && o && (p->state == POLYP_STATE_READY));

    while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
        p->state = POLYP_STATE_POLLING;
        err = pa_mainloop_iterate(p->mainloop, 1, NULL);
        p->state = POLYP_STATE_READY;
        if (err < 0)
            return err;
    }

    return 0;
}

int polyp_wait_stream_state(snd_polyp_t *p, pa_stream *stream, pa_stream_state_t target)
{
    int err;
    pa_stream_state_t state;

    assert(p && stream && (p->state == POLYP_STATE_READY));

    while (1) {
        state = pa_stream_get_state(stream);

        if (state == PA_STREAM_FAILED)
            return -EIO;

        if (state == target)
            break;

        p->state = POLYP_STATE_POLLING;
        err = pa_mainloop_iterate(p->mainloop, 1, NULL);
        p->state = POLYP_STATE_READY;
        if (err < 0)
            return -EIO;
    }

    return 0;
}

snd_polyp_t *polyp_new()
{
    snd_polyp_t *p;

    p = calloc(1, sizeof(snd_polyp_t));
    assert(p);

    p->state = POLYP_STATE_INIT;

    p->main_fd = -1;
    p->thread_fd = -1;
    p->thread_running = 0;

    p->mainloop = pa_mainloop_new();
    assert(p->mainloop);

    p->context = pa_context_new(pa_mainloop_get_api(p->mainloop),
        "ALSA Plugin");
    assert(p->context);

    return p;
}

void polyp_free(snd_polyp_t *p)
{
    if (p->thread_running) {
        assert(p->mainloop && p->thread);
        write_command(p, COMMAND_QUIT);
        pa_mainloop_wakeup(p->mainloop);
        pthread_join(p->thread, NULL);
    }

    if (p->context)
        pa_context_unref(p->context);
    if (p->mainloop)
        pa_mainloop_free(p->mainloop);

    if (p->thread_fd >= 0)
        close(p->thread_fd);
    if (p->main_fd >= 0)
        close(p->main_fd);

    free(p);
}

int polyp_connect(snd_polyp_t *p, const char *server)
{
    int err;
    pa_context_state_t state;

    assert(p && p->context && p->mainloop && (p->state == POLYP_STATE_INIT));

    err = pa_context_connect(p->context, server, 0, NULL);
    if (err < 0)
        goto error;

    while (1) {
        state = pa_context_get_state(p->context);

        if (state == PA_CONTEXT_FAILED)
            goto error;

        if (state == PA_CONTEXT_READY)
            break;

        err = pa_mainloop_iterate(p->mainloop, 1, NULL);
        if (err < 0)
            return -EIO;
    }

    p->state = POLYP_STATE_CONNECTED;

    return 0;

error:
    fprintf(stderr, "*** POLYPAUDIO: Unable to connect: %s\n",
        pa_strerror(pa_context_errno(p->context)));
    return -ECONNREFUSED;
}

int polyp_start_thread(snd_polyp_t *p)
{
    int err;
	int fd[2] = { -1, -1 };

    assert(p && (p->state == POLYP_STATE_CONNECTED));

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) < 0) {
        perror("socketpair()");
        return -errno;
    }

    p->thread_fd = fd[0];
    p->main_fd = fd[1];

    p->thread_running = 0;

    err = pthread_create(&p->thread, NULL, thread_func, p);
    if (err) {
        SNDERR("pthread_create(): %s", strerror(err));
        close(fd[0]);
        close(fd[1]);
        p->main_fd = -1;
        p->thread_fd = -1;
        return -err;
    }

    p->thread_running = 1;

    p->state = POLYP_STATE_READY;

    return 0;
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
	int err;

    assert(p);

    err = polyp_finish_poll(p);
    if (err < 0)
        return err;

    err = polyp_start_poll(p);
    if (err < 0)
        return err;

    assert(space >= 1);

    pfd[0].fd = p->main_fd;
    pfd[0].events = POLL_IN;
    pfd[0].revents = 0;

    return 1;
}

int polyp_poll_revents(snd_polyp_t *p, struct pollfd *pfd, unsigned int nfds, unsigned short *revents)
{
	int err;

    assert(p);

    err = polyp_finish_poll(p);
    if (err < 0)
        return err;

    err = polyp_check_connection(p);
    if (err < 0)
        return err;

    /*
     * The application might redo the poll immediatly.
     */
    return polyp_poll_descriptors(p, pfd, nfds);
}
