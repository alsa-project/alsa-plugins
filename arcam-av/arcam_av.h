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

#include <pthread.h>

typedef enum {
	ARCAM_AV_ZONE1				= '1',
	ARCAM_AV_ZONE2				= '2'
} arcam_av_zone_t;


typedef enum {
	ARCAM_AV_POWER				= '*',
	ARCAM_AV_VOLUME_CHANGE			= '/',
	ARCAM_AV_VOLUME_SET			= '0',
	ARCAM_AV_MUTE				= '.',
	ARCAM_AV_SOURCE				= '1',
	ARCAM_AV_SOURCE_TYPE			= '7',
	ARCAM_AV_DIRECT				= '3',
	ARCAM_AV_STEREO_DECODE			= '4',
	ARCAM_AV_MULTI_DECODE			= '5',
	ARCAM_AV_STEREO_EFFECT			= '6'
} arcam_av_cc_t;


typedef enum {
	ARCAM_AV_OK				= 'P',
	ARCAM_AV_ERROR				= 'R'
} arcam_av_rc_t;


typedef enum {
	ARCAM_AV_POWER_STAND_BY			= '0',
	ARCAM_AV_POWER_ON			= '1',
	ARCAM_AV_POWER_REQUEST			= '9'
} arcam_av_power_t;


typedef enum {
	ARCAM_AV_VOLUME_MIN			= '0',
	ARCAM_AV_VOLUME_REQUEST			= '9'
} arcam_av_volume_t;


typedef enum {
	ARCAM_AV_MUTE_ON			= '0',
	ARCAM_AV_MUTE_OFF			= '1',
	ARCAM_AV_MUTE_REQUEST			= '9'
} arcam_av_mute_t;


typedef enum {
	ARCAM_AV_DIRECT_DISABLE			= '0',
	ARCAM_AV_DIRECT_ENABLE			= '1',
	ARCAM_AV_DIRECT_REQUEST			= '9'
} arcam_av_direct_t;


typedef enum {
	ARCAM_AV_SOURCE_DVD 			= '0',
	ARCAM_AV_SOURCE_SAT			= '1',
	ARCAM_AV_SOURCE_AV			= '2',
	ARCAM_AV_SOURCE_PVR			= '3',
	ARCAM_AV_SOURCE_VCR			= '4',
	ARCAM_AV_SOURCE_CD			= '5',
	ARCAM_AV_SOURCE_FM			= '6',
	ARCAM_AV_SOURCE_AM			= '7',
	ARCAM_AV_SOURCE_DVDA			= '8',
	ARCAM_AV_SOURCE_REQUEST			= '9'
} arcam_av_source_t;


typedef enum {
	ARCAM_AV_SOURCE_TYPE_ANALOGUE 		= '0',
	ARCAM_AV_SOURCE_TYPE_DIGITAL		= '1',
	ARCAM_AV_SOURCE_TYPE_REQUEST		= '9'
} arcam_av_source_type_t;


typedef enum {
	ARCAM_AV_STEREO_DECODE_MONO		= '.',
	ARCAM_AV_STEREO_DECODE_STEREO		= '/',
	ARCAM_AV_STEREO_DECODE_PLII_MOVIE	= '0',
	ARCAM_AV_STEREO_DECODE_PLII_MUSIC	= '1',
	ARCAM_AV_STEREO_DECODE_PLIIx_MOVIE	= '3',
	ARCAM_AV_STEREO_DECODE_PLIIx_MUSIC	= '4',
	ARCAM_AV_STEREO_DECODE_DOLBY_PL		= '6',
	ARCAM_AV_STEREO_DECODE_NEO6_CINEMA	= '7',
	ARCAM_AV_STEREO_DECODE_NEO6_MUSIC	= '8',
	ARCAM_AV_STEREO_DECODE_REQUEST		= '9'
} arcam_av_stereo_decode_t;


typedef enum {
	ARCAM_AV_MULTI_DECODE_MONO		= '.',
	ARCAM_AV_MULTI_DECODE_STEREO		= '/',
	ARCAM_AV_MULTI_DECODE_MULTI_CHANNEL	= '0',
	ARCAM_AV_MULTI_DECODE_PLIIx		= '2',
	ARCAM_AV_MULTI_DECODE_REQUEST		= '9'
} arcam_av_multi_decode_t;


typedef enum {
	ARCAM_AV_STEREO_EFFECT_NONE		= '0',
	ARCAM_AV_STEREO_EFFECT_MUSIC		= '1',
	ARCAM_AV_STEREO_EFFECT_PARTY		= '2',
	ARCAM_AV_STEREO_EFFECT_CLUB		= '3',
	ARCAM_AV_STEREO_EFFECT_HALL		= '4',
	ARCAM_AV_STEREO_EFFECT_SPORTS		= '5',
	ARCAM_AV_STEREO_EFFECT_CHURCH		= '6',
	ARCAM_AV_STEREO_EFFECT_REQUEST		= '9'
} arcam_av_stereo_effect_t;

int arcam_av_connect(const char* port);
int arcam_av_send(int fd, arcam_av_cc_t command, unsigned char param1, unsigned char param2);


typedef struct arcam_av_state {
	union {
		struct {
			unsigned char		power;
			unsigned char		volume;
			unsigned char		mute;
			unsigned char		direct;
			unsigned char		source;
			unsigned char		source_type;
			unsigned char		stereo_decode;
			unsigned char		stereo_effect;
			unsigned char		multi_decode;
		};
		unsigned char			state[9];
	} zone1;
	union {
		struct {
			unsigned char		power;
			unsigned char		volume;
			unsigned char		mute;
			unsigned char		source;
		};
		unsigned char			state[4];
	} zone2;
} arcam_av_state_t;

arcam_av_state_t* arcam_av_state_attach(const char* port);
int arcam_av_state_detach(arcam_av_state_t* state);

int arcam_av_server_start(pthread_t* thread, const char* port);
int arcam_av_server_stop(pthread_t thread, const char* port);

int arcam_av_client(const char* port);
