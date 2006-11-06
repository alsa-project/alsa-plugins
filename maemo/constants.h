/**
 * @file constants.h 
 * @brief PCM Task node protocol constants definition
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
#ifndef _CONSTANTS_H
#define _CONSTANTS_H
/**
 * Commands
 * */
/** No command */
#define DSP_CMD_NONE 		0x00
/** Informs the DSP that the following data is about initialisation. */
#define DSP_CMD_INIT		0x01
/** Informs the DSP that the following data is parameters */
#define DSP_CMD_SET_PARAMS	0x02
/** Informs the DSP that the following data is general data (compressed 
 * or raw audio or video) 
 * */
#define DSP_CMD_DATA_WRITE	0x03
/** Starts audio or video playback or recording */
#define DSP_CMD_PLAY		0x04
/** Pauses playback */
#define DSP_CMD_PAUSE		0x05
/** Stops playback */
#define DSP_CMD_STOP		0x06
/** Informs the DSP that the following data is volume */
#define DSP_CMD_SET_VOLUME	0x07
/** Requests from the DSP to send information about current task node 
 * state 
 * */
#define DSP_CMD_STATE		0x08
/** Informs the DSP that the following data is about setting the current
 *  presentation time 
 * */
#define DSP_CMD_SET_TIME	0x09
/** Informs the DSP that the ARM queries the current presentation time */
#define DSP_CMD_GET_TIME	0x0A
/** Informs the DSP that the following data is about setting video 
 * post-processing parameters 
 * */
#define DSP_CMD_SET_POSTPROC	0x0B
/** Informs the DSP that the following data is about setting the panning
 * */
#define DSP_CMD_SET_PANNING	0x0D
/** Informs the DSP about discontinuity in the audio stream */
#define DSP_CMD_DISCONT		0x0E
/** Mutes the audio playback */
#define DSP_CMD_MUTE		0x0F
/** Unmutes the audio playback */
#define DSP_CMD_UNMUTE		0x10
/**Closes the task node*/
#define DSP_CMD_CLOSE		0x14
/** Command from DSP to start to read data*/
#define DSP_CMD_DATA_READ	0x25
/**Sets speech parameters*/
#define DSP_CMD_SET_SPEECH_PARAMS 0x26

/**
 * Audio formats
 * */
/** Unsigned 8 bits per sample PCM */
#define DSP_AFMT_U8		0x01
/** Signed 16 bits per sample PCM, little endian */
#define DSP_AFMT_S16_LE		0x02
/** Signed 16 bits per sample PCM, big endian */
#define DSP_AFMT_S16_BE		0x03
/** Signed 8 bits per sample PCM */
#define DSP_AFMT_S8		0x04
/** Unsigned 16 bits per sample PCM, little endian */
#define DSP_AFMT_U16_LE		0x05
/** Unsigned 16 bits per sample PCM, big endian */
#define DSP_AFMT_U16_BE		0x06
/** A-law encoded PCM */
#define DSP_AFMT_ALAW		0x07
/** μ-Law encoded PCM */
#define DSP_AFMT_ULAW		0x08
/** MP3 stream */
#define DSP_AFMT_MP3		0x09
/** AAC stream */
#define DSP_AFMT_AAC		0x0A
/** AMR stream */
#define DSP_AFMT_AMR		0x0B
/** MP2 stream */
#define DSP_AFMT_MP2		0x0C
/** iLBC stream */
#define DSP_AFMT_ILBC		0x0D
/** G.729 stream */
#define DSP_AFMT_G729		0x0E
/**
 * Supported Sample rates
 * */
/** 96KHz sampling rate */
#define SAMPLE_RATE_96KHZ	0x00
/** 88.2KHz sampling rate */
#define SAMPLE_RATE_88_2KHZ	0x01
/** 64KHz sampling rate */
#define SAMPLE_RATE_64KHZ	0x02
/** 48KHz sampling rate */
#define SAMPLE_RATE_48KHZ	0x03
/** 44.1KHz sampling rate */
#define SAMPLE_RATE_44_1KHZ	0x04
/** 32KHz sampling rate */
#define SAMPLE_RATE_32KHZ	0x05
/** 24KHz sampling rate */
#define SAMPLE_RATE_24KHZ	0x06
/** 22.05KHz sampling rate */
#define SAMPLE_RATE_22_05KHZ	0x07
/** 16KHz sampling rate */
#define SAMPLE_RATE_16KHZ	0x08
/** 12KHz sampling rate */
#define SAMPLE_RATE_12KHZ	0x09
/** 11.025KHz sampling rate */
#define SAMPLE_RATE_11_025KHZ	0x0A
/** 8KHz sampling rate */
#define SAMPLE_RATE_8KHZ	0x0B
/** 5.5125Khz sampling rate */
#define SAMPLE_RATE_5_5125KHZ	0X0C
/**
 * DSP Return values
 * */
/** Operation successful */
#define DSP_OK 			0x01
/** Unrecognised or unsupported command value */
#define DSP_ERROR_CMD		0x02
/** Unrecognised or unsupported audio format value */
#define DSP_ERROR_FMT		0x03
/** Unrecognised or unsupported sampling rate value */
#define DSP_ERROR_RATE		0x04
/** Unrecognised or unsupported number of channels */
#define DSP_ERROR_CHANNELS	0x05
/** Destination/source stream ID out of range */
#define DSP_ERROR_DS_ID		0x06
/** Insufficient memory to perform requested action */
#define DSP_ERROR_MEMORY 	0x07
/** Unspecified error */
#define DSP_ERROR_GENERAL	0x08
/** Error in stream (audio or video) */
#define DSP_ERROR_STREAM 	0x09
/** Unexpected task node state */
#define DSP_ERROR_STATE 	0x0A
/** Error in synchronisation: 
   For MP3 – synchronisation marker not found */
#define DSP_ERROR_SYNC		0x0B
/** For MPEG4: non-compliant video stream */
#define DSP_ERROR_VIDEO_NON_COMPLIANT 0x100
/** For MPEG4: Error in VOS */
#define DSP_ERROR_VIDEO_FAULT_IN_VOS  0x101
/** For MPEG4: Image size not supported */
#define DSP_ERROR_VIDEO_SIZE_NOT_SUPPORTED 0x102
/** End of VOS code reached */
#define DSP_ERROR_VIDEO_VOS_END_CODE 0x103
/**
 * Channels
 * */
/** One channel (mono) */
#define CHANNELS_1 		0x01
/** Two channels (stereo) */
#define CHANNELS_2		0x02
/**
 * Audio Task node states
 * */
/** Initialised */
#define STATE_INITIALISED	0x00
/** Playing/recording */
#define STATE_PLAYING		0x01
/** Stopped */
#define STATE_STOPPED		0x02
/** Paused */
#define STATE_PAUSED		0x03
/** Not initialised */
#define STATE_UNINITIALISED	0x04
/** Reseted */
#define STATE_RESET		0x05
/** Muted */
#define STATE_MUTED		0x06

/** Sending commands */
#define REQUEST_CONFIRMATION	0x01
#define WITHOU_CONFIRMATION	0x00
#endif				/* _CONSTANTS_H */
