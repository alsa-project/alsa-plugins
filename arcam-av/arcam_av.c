/*
 * ALSA -> Arcam AV control plugin
 *
 * Copyright (c) 2009 by Peter Stokes <linux@dadeos.co.uk>
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


#include "arcam_av.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define MAX(a, b)     ((a) > (b) ? (a) : (b))


int arcam_av_connect(const char* port)
{
	struct termios portsettings;

	int fd = open(port, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -errno;

	bzero(&portsettings, sizeof(portsettings));
	portsettings.c_cflag = B38400 | CS8 | CLOCAL | CREAD;
	portsettings.c_iflag = IGNPAR;
	portsettings.c_oflag = 0;
	portsettings.c_lflag = 0;
	portsettings.c_cc[VTIME] = 0;
	portsettings.c_cc[VMIN] = 5;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &portsettings);

	return fd;
}


int arcam_av_send(int fd, arcam_av_cc_t command, unsigned char param1, unsigned char param2)
{
	const char buffer[7] = {'P', 'C', '_', command, param1, param2, 0x0D};
	const char* cursor = buffer;

	tcdrain(fd);

	do {
		ssize_t bytes = write(fd, cursor, sizeof buffer - (cursor - buffer));

		if (bytes <= 0)
			return -errno;
		
		cursor += bytes;

	} while (cursor < buffer + sizeof buffer);

	return 0;
}


static int arcam_av_receive(int fd, arcam_av_cc_t* command, unsigned char* param1, unsigned char* param2)
{
	static int index = 0;
	static arcam_av_cc_t received_command;
	static unsigned char received_param1;
	static unsigned char received_param2;

	do {
		static char buffer[8];
		char* cursor = buffer;

		ssize_t bytes = read(fd, buffer, sizeof buffer - index);

		if (bytes <= 0)
			return -errno;

		while(bytes > 0) {
			switch(index++) {
			case 0:
				if (*cursor != 'A')
					index = 0;
				break;

			case 1:
				if (*cursor != 'V') {
					index = 0;
					continue;
				}
				break;

			case 2:
				if (*cursor != '_') {
					index = 0;
					continue;
				}
				break;

			case 3:
				received_command = *cursor;
				break;

			case 4:
				if (*cursor != ARCAM_AV_OK) {
					index = 0;
					continue;
				}
				break;

			case 5:
				received_param1 = *cursor;
				break;

			case 6:
				received_param2 = *cursor;
				break;

			case 7:
				if (*cursor != 0x0D) {
					index = 0;
					continue;
				}
				break;
			}

			++cursor;
			--bytes;
		}
	} while (index < 8);

	index = 0;
	*command = received_command;
	*param1 = received_param1;
	*param2 = received_param2;

	return 0;
}


static int arcam_av_update(arcam_av_state_t* state, int fd)
{
	int result = -1;

	arcam_av_cc_t command = 0;
	unsigned char param1 = 0;
	unsigned char param2 = 0;

	while (!arcam_av_receive(fd, &command, &param1, &param2)) {
		switch(command) {
		case ARCAM_AV_POWER:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.power = param2;
				result = 0;
				break;

			case ARCAM_AV_ZONE2:
				state->zone2.power = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_VOLUME_CHANGE:
		case ARCAM_AV_VOLUME_SET:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.volume = param2;
				result = 0;
				break;

			case ARCAM_AV_ZONE2:
				state->zone2.volume = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_MUTE:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.mute = param2;
				result = 0;
				break;

			case ARCAM_AV_ZONE2:
				state->zone2.mute = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_DIRECT:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.direct = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_SOURCE:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.source = param2;
				result = 0;
				break;

			case ARCAM_AV_ZONE2:
				state->zone2.source = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_SOURCE_TYPE:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.source_type = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_STEREO_DECODE:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.stereo_decode = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_STEREO_EFFECT:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.stereo_effect = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		case ARCAM_AV_MULTI_DECODE:
			switch(param1) {
			case ARCAM_AV_ZONE1:
				state->zone1.multi_decode = param2;
				result = 0;
				break;

			default:
				break;
			}
			break;

		default:
			break;
		}
	}

	return result;
}


static void arcam_av_state_query(int fd)
{
	arcam_av_send(fd, ARCAM_AV_POWER, ARCAM_AV_ZONE1, ARCAM_AV_POWER_REQUEST);
	arcam_av_send(fd, ARCAM_AV_VOLUME_CHANGE, ARCAM_AV_ZONE1, ARCAM_AV_VOLUME_REQUEST);
	arcam_av_send(fd, ARCAM_AV_MUTE, ARCAM_AV_ZONE1, ARCAM_AV_MUTE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_DIRECT, ARCAM_AV_ZONE1, ARCAM_AV_DIRECT_REQUEST);
	arcam_av_send(fd, ARCAM_AV_SOURCE, ARCAM_AV_ZONE1, ARCAM_AV_SOURCE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_SOURCE_TYPE, ARCAM_AV_ZONE1, ARCAM_AV_SOURCE_TYPE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_STEREO_DECODE, ARCAM_AV_ZONE1, ARCAM_AV_STEREO_DECODE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_MULTI_DECODE, ARCAM_AV_ZONE1, ARCAM_AV_MULTI_DECODE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_STEREO_EFFECT, ARCAM_AV_ZONE1, ARCAM_AV_STEREO_EFFECT_REQUEST);

	arcam_av_send(fd, ARCAM_AV_POWER, ARCAM_AV_ZONE2, ARCAM_AV_POWER_REQUEST);
	arcam_av_send(fd, ARCAM_AV_VOLUME_CHANGE, ARCAM_AV_ZONE2, ARCAM_AV_VOLUME_REQUEST);
	arcam_av_send(fd, ARCAM_AV_MUTE, ARCAM_AV_ZONE2, ARCAM_AV_MUTE_REQUEST);
	arcam_av_send(fd, ARCAM_AV_SOURCE, ARCAM_AV_ZONE2, ARCAM_AV_SOURCE_REQUEST);
}


arcam_av_state_t* arcam_av_state_attach(const char* port)
{
	arcam_av_state_t* state;
	struct stat port_stat;
	key_t ipc_key;
	int shmid, shmflg;
	struct shmid_ds shm_stat;

	if (stat(port, &port_stat))
		return NULL;

	ipc_key = ftok(port, 'A');
	if (ipc_key < 0)
		return NULL;

	shmflg = IPC_CREAT | (port_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
	shmid = shmget(ipc_key, sizeof(arcam_av_state_t), shmflg);
	if (shmid < 0)
		return NULL;

	if (shmctl(shmid, IPC_STAT, &shm_stat))
		return NULL;

	shm_stat.shm_perm.uid = port_stat.st_uid;
	shm_stat.shm_perm.gid = port_stat.st_gid;
	shmctl(shmid, IPC_SET, &shm_stat);

	state = shmat(shmid, NULL, 0);

	return (state == (void*)-1) ? NULL : state;
}


int arcam_av_state_detach(arcam_av_state_t* state)
{
	return shmdt(state);
}


static void arcam_av_server_broadcast(fd_set* fds, int fd_max, void* buffer, int bytes)
{
	int fd;
	for (fd = 0; fd <= fd_max; ++fd) {
		if (FD_ISSET(fd, fds)) {
			send(fd, buffer, bytes, 0);
		}
	}
}


static int arcam_av_server_master(int server_fd)
{
	struct sockaddr_un server_address;
	socklen_t server_address_length;
	const char* port;
	int arcam_fd;
	arcam_av_state_t* state;
	fd_set all_fds, client_fds, read_fds;
	int fd, fd_max;

	int result = 0;

	server_address_length = sizeof(server_address) - 1;
	if (getsockname(server_fd, (struct sockaddr*) &server_address, &server_address_length))
		return -errno;

	server_address.sun_path[server_address_length - offsetof(struct sockaddr_un, sun_path)] = '\0';

	port = server_address.sun_path + 1;

	arcam_fd = arcam_av_connect(port);

	state = arcam_av_state_attach(port);
	if (!state) {
		close(arcam_fd);
		return -errno;
	}

	arcam_av_state_query(arcam_fd);

	fcntl(arcam_fd, F_SETFL, O_NONBLOCK);

	FD_ZERO(&all_fds);
	FD_ZERO(&client_fds);
	FD_SET(arcam_fd, &all_fds);
	FD_SET(server_fd, &all_fds);
	fd_max = MAX(arcam_fd, server_fd);

	for(;;) {
		read_fds = all_fds;

		if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) < 0) {
			perror("arcam_av_server_master(): select");
			result = -errno;
			break;
		}

		for(fd = fd_max; fd; --fd) {
			if (FD_ISSET(fd, &read_fds)) {
				if (fd == arcam_fd) {
					if (arcam_av_update(state, arcam_fd))
						continue;

					arcam_av_server_broadcast(&client_fds, fd_max, "", 1);
				} else if (fd == server_fd) {
					struct sockaddr_un client_address;
					socklen_t client_address_length = sizeof(client_address);
					int client_fd = accept(server_fd, (struct sockaddr*) &client_address, &client_address_length);

					if (client_fd >= 0) {
						FD_SET(client_fd, &all_fds);
						FD_SET(client_fd, &client_fds);
						fd_max = MAX(fd_max, client_fd);
					} else {
						perror("arcam_av_server_master(): accept");
						result = -errno;
						goto exit;
					}
				} else {
					pthread_t thread;
					int bytes = recv(fd, &thread, sizeof(pthread_t), 0);

					if (bytes > 0) {
						if (bytes == sizeof(pthread_t)) {
							if (thread == pthread_self())
								goto exit;

							arcam_av_server_broadcast(&client_fds, fd_max, &thread, sizeof(pthread_t));
						}
					} else {
						close(fd);
						FD_CLR(fd, &all_fds);
						FD_CLR(fd, &client_fds);
						fd_max -= (fd_max == fd);
					}
				}
			}
		}
	}

exit:

	for (fd = 0; fd <= fd_max; ++fd) {
		if (fd != server_fd && FD_ISSET(fd, &all_fds)) {
			close(fd);
		}
	}

	if (state)
		arcam_av_state_detach(state);

	return result;
}


static int arcam_av_server_slave(int server_fd)
{
	for (;;) {
		pthread_t thread;
		int bytes = recv(server_fd, &thread, sizeof(pthread_t), 0);

		if (bytes > 0) {
			if (bytes == sizeof(pthread_t))
				if (thread == pthread_self())
					return 0;
		} else {
			break;
		}
	}

	return -1;
}


static void* arcam_av_server_thread(void* context)
{
	struct sockaddr_un address;
	int size;
	int quit = 0;

	sem_t* semaphore = context;
	const char* port = *(const char**)(semaphore + 1);

	address.sun_family = AF_FILE;
	address.sun_path[0] = '\0';
	strncpy(&address.sun_path[1], port, sizeof(address.sun_path) - 1);
	size = offsetof(struct sockaddr_un, sun_path) +
	       MIN(strlen(port) + 1, sizeof(address.sun_path));

	signal(SIGPIPE, SIG_IGN);

	while (!quit) {
		int server_fd = socket(PF_UNIX, SOCK_STREAM, 0);
		if (server_fd < 0) {
			perror("arcam_av_server_thread(): socket");
			break;
		}

		if (!bind(server_fd, (struct sockaddr*) &address, size)) {
			if (!listen(server_fd, 10)) {
				if (semaphore) {
					sem_post(semaphore);
					semaphore = NULL;
				}
				arcam_av_server_master(server_fd);
			} else {
				perror("arcam_av_server_master(): listen");
			}
			quit = 1;
		} else if (errno != EADDRINUSE) {
			perror("arcam_av_server_thread(): bind");
			quit = 1;
		} else if (!connect(server_fd, (struct sockaddr*) &address, size)) {
			if (semaphore) {
				sem_post(semaphore);
				semaphore = NULL;
			}
			quit = !arcam_av_server_slave(server_fd);
		} else {
			perror("arcam_av_server_thread(): connect");
			quit = 1;
		}

		close(server_fd);
	}

	if (semaphore)
		sem_post(semaphore);

	return NULL;
}


int arcam_av_server_start(pthread_t* thread, const char* port)
{
	struct {
		sem_t		semaphore;
		const char*	port;
	} context;

	int result = 0;

	if (sem_init(&context.semaphore, 0, 0))
		return -1;

	context.port = port;

	if (pthread_create(thread, NULL, arcam_av_server_thread, &context))
		result = -1;
	else
		sem_wait(&context.semaphore);

	sem_destroy(&context.semaphore);

	return result;
}


int arcam_av_server_stop(pthread_t thread, const char* port)
{
	int client_fd = arcam_av_client(port);
	if (client_fd < 0)
		return -1;

	if (send(client_fd, &thread, sizeof(pthread_t), 0) > 0)
		pthread_join(thread, NULL);

	close(client_fd);
	return 0;
}


int arcam_av_client(const char* port)
{
	struct sockaddr_un address;
	int size;

	const int max_retries = 5;
	int retries = max_retries;

	int client_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (client_fd < 0)
		return -1;

	address.sun_family = AF_FILE;
	address.sun_path[0] = '\0';
	strncpy(&address.sun_path[1], port, sizeof(address.sun_path) - 1);
	size = offsetof(struct sockaddr_un, sun_path) +
	       MIN(strlen(port) + 1, sizeof(address.sun_path));

	do {
		if (!connect(client_fd, (struct sockaddr*) &address, size))
			return client_fd;

		if (!retries--)
			break;

		struct timeval sleep = {0, 10 * (max_retries - retries)};
		
		select(0, NULL, NULL, NULL, &sleep);

	} while (errno == ECONNREFUSED);

	perror("arcam_av_client(): connect");

	close(client_fd);
	return -1;
}

