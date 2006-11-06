/**
 * @file reporting.h - Definition of functions whose represents an interface
 *                     to report errors. 
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
#ifndef _REPORTING_H
#define _REPORTING_H
#include "dsp-protocol.h"

#ifdef DEBUG
const char *dsp_commands[] = {
	"DSP_CMD_NONE", "No command",
	"DSP_CMD_INIT", "Informs the DSP that the following data is"
	    "about initialisation",
	"DSP_CMD_SET_PARAMS", "Informs the DSP that the following "
	    "data is parameters",
	"DSP_CMD_DATA_WRITE", "Informs the DSP that the following "
	    "data is general data (compressed " "or raw audio or video)",
	"DSP_CMD_PLAY", "Starts audio or video playback or recording",
	"DSP_CMD_PAUSE", "Pauses playback",
	"DSP_CMD_STOP", "Stops playback",
	"DSP_CMD_SET_VOLUME", "Informs the DSP that the following "
	    "data is volume",
	"DSP_CMD_STATE", "Requests from the DSP to send information"
	    " about current task node state",
	"DSP_CMD_SET_TIME", "Informs the DSP that the following data"
	    " is about setting the current" " presentation time",
	"DSP_CMD_GET_TIME", "Informs the DSP that the ARM queries the"
	    " current presentation time",
	"ERROR", "This is unused!!!!!",
	"DSP_CMD_SET_POSTPROC", "Informs the DSP that the following data"
	    " is about setting video post-processing " "parameters",
	"DSP_CMD_SET_PANNING", "Informs the DSP that the following data "
	    "is about setting the panning",
	"DSP_CMD_DISCONT", "Informs the DSP about discontinuity in the "
	    "audio stream",
	"DSP_CMD_MUTE", "Mutes the audio playback",
	"DSP_CMD_UNMUTE", "Unmutes the audio playback",
	"ERROR", "This is unused!!!!!",
	"ERROR", "This is unused!!!!!",
	"ERROR", "This is unused!!!!!",
	"DSP_CMD_CLOSE", "Closes the task node"
};

const char *dsp_return_values[] = {
	"DSP_NONE", "Error. This isn't a valid return value",
	"DSP_OK", "Operation successful",
	"DSP_ERROR_CMD", "Unrecognised or unsupported command value",
	"DSP_ERROR_FMT", "Unrecognised or unsupported audio format value",
	"DSP_ERROR_RATE", "Unrecognised or unsupported sampling rate value",
	"DSP_ERROR_CHANNELS", "Unrecognised or unsupported number of channels",
	"DSP_ERROR_DS_id", "Destination/source stream id out of range",
	"DSP_ERROR_MEMORY", "Insufficient memory to perform requested action",
	"DSP_ERROR_GENERAL", "Unspecified error",
	"DSP_ERROR_STREAM", "Error in stream (audio or video)",
	"DSP_ERROR_STATE", "Unexpected task node state",
	"DSP_ERROR_SYNC", "Error in synchronisation:"
	    "For MP3 – synchronisation marker not found"
};

const char *dsp_states[] = {
	"STATE_INITIALISED", "Initialised",
	"STATE_PLAYING", "Playing/recording",
	"STATE_STOPPED", "Stopped",
	"STATE_PAUSED", "Paused",
	"STATE_UNINITIALISED", "Not initialised",
	"STATE_RESET", "Reseted",
	"STATE_MUTED", "Muted"
};

const char *dsp_rates[] = {
	"SAMPLE_RATE_96KHZ", "96KHz sampling rate",
	"SAMPLE_RATE_88_2KHZ", "88.2KHz sampling rate",
	"SAMPLE_RATE_64KHZ", "64KHz sampling rate",
	"SAMPLE_RATE_48KHZ", "48KHz sampling rate",
	"SAMPLE_RATE_44_1KHZ", "44.1KHz sampling rate",
	"SAMPLE_RATE_32KHZ", "32KHz sampling rate",
	"SAMPLE_RATE_24KHZ", "24KHz sampling rate",
	"SAMPLE_RATE_22_05KHZ", "22.05KHz sampling rate",
	"SAMPLE_RATE_16KHZ", "16KHz sampling rate",
	"SAMPLE_RATE_12KHZ", "12KHz sampling rate",
	"SAMPLE_RATE_11_025KHZ", "11.025KHz sampling rate",
	"SAMPLE_RATE_8KHZ", "8KHz sampling rate",
	"SAMPLE_RATE_5_5125KHZ", "5.5125Khz sampling rate"
};

const char *dsp_channels[] = {
	"0--", "Error - No channel!",
	"CHANNELS_1", "One channel (mono)",
	"CHANNELS_2", "Two channels (stereo)"
};

const char *dsp_audio_fmt[] = {
	"0", "Error No format!!!",
	"DSP_AFMT_U8", "Unsigned 8 bits per sample PCM",
	"DSP_AFMT_S16_LE", "Signed 16 bits per sample PCM, little endian",
	"DSP_AFMT_S16_BE", "Signed 16 bits per sample PCM, big endian",
	"DSP_AFMT_S8", "Signed 8 bits per sample PCM",
	"DSP_AFMT_U16_LE", "Unsigned 16 bits per sample PCM, little endian",
	"DSP_AFMT_U16_BE", "Unsigned 16 bits per sample PCM, big endian",
	"DSP_AFMT_ALAW", "A-law encoded PCM",
	"DSP_AFMT_ULAW", "μ-Law encoded PCM",
	"DSP_AFMT_MP3", "MP3 stream",
	"DSP_AFMT_AAC", "AAC stream",
	"DSP_AFMT_AMR", "AMR stream",
	"DSP_AFMT_MP2", "MP2 stream",
	"DSP_AFMT_ILBC", "iLBC stream",
	"DSP_AFMT_G729", "G.729 stream"
};

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))
#define report_table(mens,name,value,table)\
	do{\
		if ((unsigned)value >= ARRAY_SIZE(table))\
			DPRINT("%s: %d isnt a valid %s value\n",mens,\
							value,name);\
		else\
			DPRINT("%s: [%d|%s] - %s\n", mens, value, \
					table[value * 2], \
					table[value * 2 + 1]);\
	}while(0)

#define report_command(m,v) 		report_table(m,"command",v,\
						/*20,*/dsp_commands)
#define report_return_value(m,v) 	report_table(m,"return",v,\
						/*11,*/dsp_return_values)
#define report_state(m,v) 		report_table(m,"state",v,\
						/*6,*/dsp_states)
#define report_sample_rate(m,v) 	report_table(m,"sample rate",v,\
						/*12,*/dsp_rates)
#define report_number_channels(m,v) 	report_table(m,"number of channels",v,\
						/*2,*/dsp_channels)
#define report_audio_fmt(m,v) 		report_table(m,"audio format",v,\
						/*14,*/dsp_audio_fmt)
#define report_dsp_protocol(m,dp)\
	do{\
		DPRINT("%s:\n"\
			"fd: %d\n"\
			"stream_id: %d\n"\
			"bridge_buffer_size: %d\n"\
  			"mmap_buffer_size: %d\n"\
  			"mmap_buffer: %p\n",\
				m,\
				dp->fd,\
				dp->stream_id,\
				dp->bridge_buffer_size,\
				dp->mmap_buffer_size,\
				dp->mmap_buffer);\
		report_state("state", dp->state);\
	}while(0)

#define report_audio_status_info(m, asi)\
	do{\
		DPRINT("%s\n", m);\
		DPRINT("***** Audio status info *****\n");\
		report_command("\tdsp_cmd", asi.dsp_cmd);\
		DPRINT("\tstream_id: %d\n", asi.stream_id);\
		DPRINT("\tds_stream_id: %d\n", asi.ds_stream_id);\
		DPRINT("\tbridge_buffer_size: %d\n", asi.bridge_buffer_size);\
		DPRINT("\tmmap_buffer_size: %d\n", asi.mmap_buffer_size);\
		report_state("\tstatus", asi.status);\
		DPRINT("\tnum_frames: %d\n", asi.num_frames);\
		report_sample_rate("\tsample_rate", asi.sample_rate);\
		report_number_channels("\tnumber_channels", \
						asi.number_channels);\
		DPRINT("\tvol_scale: %d\n", asi.vol_scale);\
		DPRINT("\tvol_power2: %d\n", asi.vol_power2);\
		DPRINT("\tleft_gain: %d\n", asi.left_gain);\
		DPRINT("\tright_gain: %d\n", asi.right_gain);\
		report_audio_fmt("\tdsp_audio_fmt", asi.dsp_audio_fmt);\
	}while(0)

#define report_audio_init_status(m, ais)\
	do{\
		DPRINT("%s\n", m);\
		DPRINT("***** Audio init status *****\n");\
		report_command("\tdsp_cmd", ais.dsp_cmd);\
		DPRINT("\tstream_id: %d\n", ais.stream_id);\
		DPRINT("\tbridge_buffer_size: %d\n", ais.bridge_buffer_size);\
		DPRINT("\tmmap_buffer_size: %d\n", ais.mmap_buffer_size);\
		report_return_value("\tinit_status", ais.init_status);\
	}while(0)

#define report_audio_params(m,ap)\
       do{\
	        DPRINT("%s\n",m);\
	        DPRINT("**** Audio parameters *****\n");\
		report_command("\tdsp_cmd",ap.dsp_cmd);\
		report_audio_fmt("\taudio_format", ap.dsp_audio_fmt);\
		report_sample_rate("\tsample_rate", ap.sample_rate);\
		DPRINT("Number of channels %d\n", ap.number_channels);\
		DPRINT("ds_stream_id: %d\n", ap.ds_stream_id);\
		DPRINT("stream_priority: %d\n", ap.stream_priority);\
       }while(0)

#define report_speech_params(m,sp)\
       do{\
	        DPRINT("%s\n",m);\
	        DPRINT("**** Speech parameters *****\n");\
		DPRINT("\tdsp_cmd 0x%x\n",sp.dsp_cmd);\
		report_audio_fmt("\taudio_format", sp.audio_fmt);\
		report_sample_rate("\tsample_rate", sp.sample_rate);\
		DPRINT("ds_stream_id: %d\n", sp.ds_stream_id);\
		DPRINT("stream_priority: %d\n", sp.stream_priority);\
		DPRINT("frame_size: %d\n", sp.frame_size);\
       }while(0)
#else

#define report_command(m,c)
#define report_return_value(m,c)
#define report_state(m,c)
#define report_sample_rate(m,sr)
#define report_number_channel(m,nc)
#define report_audio_fmt(m,af)
#define report_dsp_protocol(m,dp)
#define report_audio_status_info(m,asi)
#define report_audio_init_status(m,ais)
#define report_audio_params(m,ap)
#define report_speech_params(m,ap)

#endif				/* _DEBUG */

#endif				/* _REPORTING_H */
