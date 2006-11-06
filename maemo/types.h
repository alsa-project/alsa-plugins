/**
 * @file types.h - datatypes defined to communicate with PCM task 
 * 		node using its protocol. 
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
#ifndef _TYPES_H
#define _TYPES_H

typedef struct {
	unsigned short dsp_cmd;
	unsigned short init_status;
	unsigned short stream_id;
	unsigned short bridge_buffer_size;
	unsigned short mmap_buffer_size;
} audio_init_status_t;

typedef struct {
	unsigned short dsp_cmd;
	unsigned short dsp_audio_fmt;
	unsigned short sample_rate;
	unsigned short number_channels;
	unsigned short ds_stream_id;
	unsigned short stream_priority;
} audio_params_data_t;

typedef struct {
	unsigned short dsp_cmd;	//DSP_CMD_SET_SPEECH_PARAMS
	unsigned short audio_fmt;	//S16_LE,ALAW,ULAW,ILBC
	unsigned short sample_rate;
	unsigned short ds_stream_id;	//Stream ID from EAP mixer
	unsigned short stream_priority;	//N/A
	unsigned short frame_size;
} speech_params_data_t;

typedef struct {
	unsigned short dsp_cmd;
	unsigned short stream_id;
	unsigned short ds_stream_id;
	unsigned short bridge_buffer_size;
	unsigned short mmap_buffer_size;
	unsigned short status;
	unsigned int num_frames;
	unsigned short sample_rate;
	unsigned short number_channels;
	unsigned short vol_scale;
	unsigned short vol_power2;
	unsigned short left_gain;
	unsigned short right_gain;
	unsigned short dsp_audio_fmt;
#ifndef NORMAL_DSP_TASK
	unsigned short mute;
	unsigned long int samples_played_high;
	unsigned long int samples_played_low;
#endif
} audio_status_info_t;

typedef struct {
	unsigned short int dsp_cmd;
	unsigned short int status;
} dsp_cmd_status_t;

/* data write status structure */
typedef struct {
	unsigned short int dsp_cmd;
	unsigned short int status;
	unsigned short int buffer_bytes_free;
} write_status_t;

typedef struct {
	unsigned short dsp_cmd;
	unsigned short data_size;
} data_write_t;

typedef struct {
	unsigned short int dsp_cmd;
	unsigned short int status;
	unsigned short int frame_size;
	unsigned int stream_time_ms;
} read_status_t;

/* get time data structure */
typedef struct {
	unsigned short int dsp_cmd;
	unsigned short int status;
	unsigned short int stream_ID;
	long int time_ms;
} time_data_t;

/* general stream command data structure */
typedef struct {
	unsigned short int dsp_cmd;
	unsigned short int stream_ID;
} stream_cmd_data_t;

typedef struct {
	unsigned short dsp_cmd;
	unsigned short scale;
	unsigned short power2;
} volume_data_t;

typedef struct {
	unsigned short dsp_cmd;
	unsigned short left_gain;
	unsigned short right_gain;
	unsigned short steps;
} panning_data_t;

#endif				/* _TYPES_H */
