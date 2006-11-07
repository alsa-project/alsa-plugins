/**
 * @file dsp-protocol.h - Definition of functions whose represents 
 *                  an interface to the DSP PCM Task node protocol. 
 * <p>
 * Copyright (C) 2006 Nokia Corporation
 * <p>
 * Contact: Eduardo Bezerra Valentin <eduardo.valentin@indt.org.br>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * */
#ifndef _DSP_PROTOCOL_H
#define _DSP_PROTOCOL_H

#define __USE_GNU
#include <features.h>
#include <pthread.h>
#include <semaphore.h>
#include "types.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


typedef struct {
	int fd;
	char *device;
	int state;
	int mute;
	unsigned int stream_id;
	unsigned int bridge_buffer_size;	/* in bytes */
	unsigned int mmap_buffer_size;	/* in bytes */
	short int *mmap_buffer;
	pthread_mutex_t mutex;
	int sem_set_id;
#ifdef USE_RESOURCE_MANAGER
	void *dbus_connection;
#endif
} dsp_protocol_t;
/* Initialisation phase */
int dsp_protocol_create(dsp_protocol_t ** dsp_protocol);
int dsp_protocol_open_node(dsp_protocol_t * dsp_protocol, const char *device);
int dsp_protocol_send_audio_params(dsp_protocol_t * dsp_protocol,
				   audio_params_data_t * audio_params_data);
int dsp_protocol_send_speech_params(dsp_protocol_t * dsp_protocol,
				    speech_params_data_t * audio_params_data);

/* Execution phase */
int dsp_protocol_send_play(dsp_protocol_t * dsp_protocol);
int dsp_protocol_send_audio_data(dsp_protocol_t * dsp_protocol,
				 void *data, unsigned short int count);
int dsp_protocol_receive_audio_data(dsp_protocol_t * dsp_protocol,
				    void *data, int count);

int dsp_protocol_send_pause(dsp_protocol_t * dsp_protocol);
int dsp_protocol_send_stop(dsp_protocol_t * dsp_protocol);

/* Deletion phase */
int dsp_protocol_close_node(dsp_protocol_t * dsp_protocol);
int dsp_protocol_destroy(dsp_protocol_t ** dsp_protocol);

/* controls */
int dsp_protocol_set_volume(dsp_protocol_t * dsp_protocol,
			    unsigned char left, unsigned char right);
int dsp_protocol_get_volume(dsp_protocol_t * dsp_protocol,
			    unsigned char *left, unsigned char *right);
int dsp_protocol_set_mute(dsp_protocol_t * dsp_protocol, unsigned char mute);
int dsp_protocol_get_mute(dsp_protocol_t * dsp_protocol);

/*miscelaneos*/
int dsp_protocol_set_mic_enabled(dsp_protocol_t * dsp_protocol, int enabled);
int dsp_protocol_probe_node(dsp_protocol_t * dsp_protocol, const char *device);
int safe_strtol(const char *str, long *val);

#endif				/* _DSP_PROTOCOL_H */
