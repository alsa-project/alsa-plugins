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

#include <sys/socket.h>
#include <alsa/asoundlib.h>
#include <alsa/control_external.h>

#include "arcam_av.h"


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define MID(a, b, c)  ((a) < (b) ? ((b) < (c) ? (b) : ((a) < (c) ? (c) : (a))) \
                                 : ((b) > (c) ? (b) : ((a) > (c) ? (c) : (a))))


static const char* arcam_av_name =		"Arcam AV";

static const struct {
	arcam_av_cc_t			code;
	const char*			name;
} arcam_av_zone1[] = {
	{ARCAM_AV_POWER,			"Power Switch"				},
	{ARCAM_AV_VOLUME_SET,			"Master Playback Volume"		},
	{ARCAM_AV_MUTE,				"Master Playback Switch"		},
	{ARCAM_AV_DIRECT,			"Direct Playback Switch"		},
	{ARCAM_AV_SOURCE,			"Source Playback Route"			},
	{ARCAM_AV_SOURCE_TYPE,			"Source Type Playback Route"		},
	{ARCAM_AV_STEREO_DECODE,		"Stereo Decode Playback Route"		},
	{ARCAM_AV_MULTI_DECODE,			"Multi-Channel Decode Playback Route"	},
	{ARCAM_AV_STEREO_EFFECT,		"Stereo Effect Playback Route"		}
};

static const struct {
	arcam_av_cc_t			code;
	const char*			name;
} arcam_av_zone2[] = {
	{ARCAM_AV_POWER,			"Power Switch"				},
	{ARCAM_AV_VOLUME_SET,			"Master Playback Volume"		},
	{ARCAM_AV_MUTE,				"Master Playback Switch"		},
	{ARCAM_AV_SOURCE,			"Source Playback Route"			}
};

static const struct {
	arcam_av_source_t		code;
	const char*			name;
} arcam_av_sources[] = {
	{ARCAM_AV_SOURCE_DVD,			"DVD"			},
	{ARCAM_AV_SOURCE_SAT,			"SAT"			},
	{ARCAM_AV_SOURCE_AV,			"AV"			},
	{ARCAM_AV_SOURCE_PVR,			"PVR"			},
	{ARCAM_AV_SOURCE_VCR,			"VCR"			},
	{ARCAM_AV_SOURCE_CD,			"CD"			},
	{ARCAM_AV_SOURCE_FM,			"FM"			},
	{ARCAM_AV_SOURCE_AM,			"AM"			},
	{ARCAM_AV_SOURCE_DVDA,			"DVDA"			}
};

static const struct {
	arcam_av_source_type_t		code;
	const char*			name;
} arcam_av_source_types[] = {
	{ARCAM_AV_SOURCE_TYPE_ANALOGUE,		"Analogue"		},
	{ARCAM_AV_SOURCE_TYPE_DIGITAL,		"Digital"		}
};

static const struct {
	arcam_av_direct_t		code;
	const char*			name;
} arcam_av_direct[] = {
	{ARCAM_AV_DIRECT_DISABLE,		"Disable"		},
	{ARCAM_AV_DIRECT_ENABLE,		"Enable"		}
};

static const struct {
	arcam_av_stereo_decode_t	code;
	const char*			name;
} arcam_av_stereo_decode_modes[] = {
	{ARCAM_AV_STEREO_DECODE_MONO,		"Mono"			},
	{ARCAM_AV_STEREO_DECODE_STEREO,		"Stereo"		},
	{ARCAM_AV_STEREO_DECODE_PLII_MOVIE,	"Pro Logic II Movie"	},
	{ARCAM_AV_STEREO_DECODE_PLII_MUSIC,	"Pro Logic II Music"	},
	{ARCAM_AV_STEREO_DECODE_PLIIx_MOVIE,	"Pro Logic IIx Movie"	},
	{ARCAM_AV_STEREO_DECODE_PLIIx_MUSIC,	"Pro Logic IIx Music"	},
	{ARCAM_AV_STEREO_DECODE_DOLBY_PL,	"Dolby Pro Logic"	},
	{ARCAM_AV_STEREO_DECODE_NEO6_CINEMA,	"Neo:6 Cinema"		},
	{ARCAM_AV_STEREO_DECODE_NEO6_MUSIC,	"Neo:6 Music"		}
};

static const struct {
	arcam_av_multi_decode_t		code;
	const char*			name;
} arcam_av_multi_decode_modes[] = {
	{ARCAM_AV_MULTI_DECODE_MONO,		"Mono down-mix"		},
	{ARCAM_AV_MULTI_DECODE_STEREO,		"Stereo down-mix"	},
	{ARCAM_AV_MULTI_DECODE_MULTI_CHANNEL,	"Multi-channel"		},
	{ARCAM_AV_MULTI_DECODE_PLIIx,		"Pro Logic IIx"		}
};

static const struct {
	arcam_av_stereo_effect_t	code;
	const char*			name;
} arcam_av_stereo_effects[] = {
	{ARCAM_AV_STEREO_EFFECT_NONE,		"None"			},
	{ARCAM_AV_STEREO_EFFECT_MUSIC,		"Music"			},
	{ARCAM_AV_STEREO_EFFECT_PARTY,		"Party"			},
	{ARCAM_AV_STEREO_EFFECT_CLUB,		"Club"			},
	{ARCAM_AV_STEREO_EFFECT_HALL,		"Hall"			},
	{ARCAM_AV_STEREO_EFFECT_SPORTS,		"Sports"		},
	{ARCAM_AV_STEREO_EFFECT_CHURCH,		"Church"		}
};


typedef struct snd_ctl_arcam_av {
	snd_ctl_ext_t		ext;
	int			port_fd;
	int			shm_id;
	const char*		port;
	arcam_av_zone_t		zone;
	arcam_av_state_t	local;
	arcam_av_state_t*	global;
	pthread_t		server;
} snd_ctl_arcam_av_t;

static void arcam_av_close(snd_ctl_ext_t *ext)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;

	if (arcam_av->port_fd >= 0)
		close(arcam_av->port_fd);

	if (arcam_av->global)
		arcam_av_state_detach(arcam_av->global);

	if (arcam_av->ext.poll_fd >= 0) {
		close(arcam_av->ext.poll_fd);
		arcam_av_server_stop(arcam_av->server, arcam_av->port);
	}

	free(arcam_av);
}

static int arcam_av_elem_count(snd_ctl_ext_t *ext)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;

	switch(arcam_av->zone) {
	case ARCAM_AV_ZONE1:
		return ARRAY_SIZE(arcam_av_zone1);

	case ARCAM_AV_ZONE2:
		return ARRAY_SIZE(arcam_av_zone2);
	}

	return 0;
}

static int arcam_av_elem_list(snd_ctl_ext_t *ext, unsigned int offset, snd_ctl_elem_id_t *id)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);

	switch(arcam_av->zone) {
	case ARCAM_AV_ZONE1:
		if (offset < ARRAY_SIZE(arcam_av_zone1))
			snd_ctl_elem_id_set_name(id, arcam_av_zone1[offset].name);
		break;

	case ARCAM_AV_ZONE2:
		if (offset < ARRAY_SIZE(arcam_av_zone2))
			snd_ctl_elem_id_set_name(id, arcam_av_zone2[offset].name);
		break;
	}

	return 0;
}

static snd_ctl_ext_key_t arcam_av_find_elem(snd_ctl_ext_t *ext,
					    const snd_ctl_elem_id_t *id)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;
	unsigned int numid, search;
	const char *name;

	numid = snd_ctl_elem_id_get_numid(id);
	if (numid > 0) {
		numid--;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			if (numid < ARRAY_SIZE(arcam_av_zone1))
				return arcam_av_zone1[numid].code;
			break;

		case ARCAM_AV_ZONE2:
			if (numid < ARRAY_SIZE(arcam_av_zone2))
				return arcam_av_zone2[numid].code;
			break;
		}
	}

	name = snd_ctl_elem_id_get_name(id);
	switch(arcam_av->zone) {
	case ARCAM_AV_ZONE1:
		for (search = 0; search < ARRAY_SIZE(arcam_av_zone1); search++)
			if (!strcmp(name, arcam_av_zone1[search].name))
				return arcam_av_zone1[search].code;
		break;

	case ARCAM_AV_ZONE2:
		for (search = 0; search < ARRAY_SIZE(arcam_av_zone2); search++)
			if (!strcmp(name, arcam_av_zone2[search].name))
				return arcam_av_zone2[search].code;
		break;
	}

	return SND_CTL_EXT_KEY_NOT_FOUND;
}

static int arcam_av_get_attribute(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
				  snd_ctl_ext_key_t key, int *type,
				  unsigned int *acc, unsigned int *count)
{
	switch(key) {
	case ARCAM_AV_POWER:
		*type = SND_CTL_ELEM_TYPE_BOOLEAN;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_VOLUME_SET:
		*type = SND_CTL_ELEM_TYPE_INTEGER;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_MUTE:
		*type = SND_CTL_ELEM_TYPE_BOOLEAN;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_DIRECT:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_SOURCE:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_SOURCE_TYPE:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_STEREO_DECODE:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_MULTI_DECODE:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	case ARCAM_AV_STEREO_EFFECT:
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int arcam_av_get_integer_info(snd_ctl_ext_t *ext,
				     snd_ctl_ext_key_t key,
				     long *imin, long *imax, long *istep)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;

	switch(key) {
	case ARCAM_AV_VOLUME_SET:
		*istep = 1;
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			*imin = 0;
			*imax = 100;
			break;

		case ARCAM_AV_ZONE2:
			*imin = 20;
			*imax = 83;
			break;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int arcam_av_get_enumerated_info(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
					snd_ctl_ext_key_t key,
					unsigned int *items)
{
	switch(key) {
	case ARCAM_AV_SOURCE:
		*items = ARRAY_SIZE(arcam_av_sources);
		break;

	case ARCAM_AV_SOURCE_TYPE:
		*items = ARRAY_SIZE(arcam_av_source_types);
		break;

	case ARCAM_AV_DIRECT:
		*items = ARRAY_SIZE(arcam_av_direct);
		break;

	case ARCAM_AV_STEREO_DECODE:
		*items = ARRAY_SIZE(arcam_av_stereo_decode_modes);
		break;

	case ARCAM_AV_MULTI_DECODE:
		*items = ARRAY_SIZE(arcam_av_multi_decode_modes);
		break;

	case ARCAM_AV_STEREO_EFFECT:
		*items = ARRAY_SIZE(arcam_av_stereo_effects);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int arcam_av_get_enumerated_name(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
					snd_ctl_ext_key_t key,
					unsigned int item, char *name,
					size_t name_max_len)
{
	const char* label;

	switch(key) {
	case ARCAM_AV_SOURCE:
		if (item >= ARRAY_SIZE(arcam_av_sources))
			return -EINVAL;

		label = arcam_av_sources[item].name;
		break;

	case ARCAM_AV_SOURCE_TYPE:
		if (item >= ARRAY_SIZE(arcam_av_source_types))
			return -EINVAL;

		label = arcam_av_source_types[item].name;
		break;

	case ARCAM_AV_DIRECT:
		if (item >= ARRAY_SIZE(arcam_av_direct))
			return -EINVAL;

		label = arcam_av_direct[item].name;
		break;

	case ARCAM_AV_STEREO_DECODE:
		if (item >= ARRAY_SIZE(arcam_av_stereo_decode_modes))
			return -EINVAL;

		label = arcam_av_stereo_decode_modes[item].name;
		break;

	case ARCAM_AV_MULTI_DECODE:
		if (item >= ARRAY_SIZE(arcam_av_multi_decode_modes))
			return -EINVAL;

		label = arcam_av_multi_decode_modes[item].name;
		break;

	case ARCAM_AV_STEREO_EFFECT:
		if (item >= ARRAY_SIZE(arcam_av_stereo_effects))
			return -EINVAL;

		label = arcam_av_stereo_effects[item].name;
		break;

	default:
		return -EINVAL;
	}

	strncpy(name, label, name_max_len - 1);
	name[name_max_len - 1] = '\0';

	return 0;
}

static int arcam_av_read_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;

	switch(key) {
	case ARCAM_AV_POWER:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.power = arcam_av->global->zone1.power;
			*value = !(arcam_av->local.zone1.power == ARCAM_AV_POWER_STAND_BY);
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.power = arcam_av->global->zone2.power;
			*value = !(arcam_av->local.zone2.power == ARCAM_AV_POWER_STAND_BY);
			break;
		}
		break;

	case ARCAM_AV_VOLUME_SET:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.volume = arcam_av->global->zone1.volume;
			*value = MID(0, arcam_av->local.zone1.volume - ARCAM_AV_VOLUME_MIN, 100);
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.volume = arcam_av->global->zone2.volume;
			*value = MID(20, arcam_av->local.zone2.volume - ARCAM_AV_VOLUME_MIN, 83);
			break;
		}
		break;

	case ARCAM_AV_MUTE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.mute = arcam_av->global->zone1.mute;
			*value = !(arcam_av->local.zone1.mute == ARCAM_AV_MUTE_ON);
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.mute = arcam_av->global->zone2.mute;
			*value = !(arcam_av->local.zone2.mute == ARCAM_AV_MUTE_ON);
			break;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int arcam_av_read_enumerated(snd_ctl_ext_t *ext,
				    snd_ctl_ext_key_t key,
				    unsigned int *item)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;
	unsigned int i;

	switch(key) {
	case ARCAM_AV_SOURCE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.source = arcam_av->global->zone1.source;
			for (i = 0; i < ARRAY_SIZE(arcam_av_sources); ++i) {
				if (arcam_av_sources[i].code == arcam_av->local.zone1.source) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.source = arcam_av->global->zone2.source;
			for (i = 0; i < ARRAY_SIZE(arcam_av_sources); ++i) {
				if (arcam_av_sources[i].code == arcam_av->local.zone2.source) {
					*item = i;
					break;
				}
			}
			break;
		}
		break;

	case ARCAM_AV_SOURCE_TYPE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.source_type = arcam_av->global->zone1.source_type;
			for (i = 0; i < ARRAY_SIZE(arcam_av_source_types); ++i) {
				if (arcam_av_source_types[i].code == arcam_av->local.zone1.source_type) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_DIRECT:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.direct = arcam_av->global->zone1.direct;
			for (i = 0; i < ARRAY_SIZE(arcam_av_direct); ++i) {
				if (arcam_av_direct[i].code == arcam_av->local.zone1.direct) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_STEREO_DECODE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.stereo_decode = arcam_av->global->zone1.stereo_decode;
			for (i = 0; i < ARRAY_SIZE(arcam_av_stereo_decode_modes); ++i) {
				if (arcam_av_stereo_decode_modes[i].code == arcam_av->local.zone1.stereo_decode) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_STEREO_EFFECT:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.stereo_effect = arcam_av->global->zone1.stereo_effect;
			for (i = 0; i < ARRAY_SIZE(arcam_av_stereo_effects); ++i) {
				if (arcam_av_stereo_effects[i].code == arcam_av->local.zone1.stereo_effect) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_MULTI_DECODE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.multi_decode = arcam_av->global->zone1.multi_decode;
			for (i = 0; i < ARRAY_SIZE(arcam_av_multi_decode_modes); ++i) {
				if (arcam_av_multi_decode_modes[i].code == arcam_av->local.zone1.multi_decode) {
					*item = i;
					break;
				}
			}
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int arcam_av_write_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;
	unsigned char volume = ARCAM_AV_VOLUME_MIN;

	switch(key) {
	case ARCAM_AV_POWER:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.power = ARCAM_AV_POWER_STAND_BY + *value;
			if (arcam_av->global->zone1.power == arcam_av->local.zone1.power)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.power = ARCAM_AV_POWER_STAND_BY + *value;
			if (arcam_av->global->zone2.power == arcam_av->local.zone2.power)
				return 0;
			break;
		}
		break;

	case ARCAM_AV_VOLUME_SET:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.volume = ARCAM_AV_VOLUME_MIN + *value;
			if (arcam_av->global->zone1.volume == arcam_av->local.zone1.volume)
				return 0;

			if (arcam_av->global->zone1.mute == ARCAM_AV_MUTE_ON) {
				arcam_av->global->zone1.volume = arcam_av->local.zone1.volume;
				return 1;
			}
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.volume = ARCAM_AV_VOLUME_MIN + *value;
			if (arcam_av->global->zone2.volume == arcam_av->local.zone2.volume)
				return 0;

			if (arcam_av->global->zone2.mute == ARCAM_AV_MUTE_ON) {
				arcam_av->global->zone2.volume = arcam_av->local.zone2.volume;
				return 1;
			}
			break;
		}
		break;

	case ARCAM_AV_MUTE:
		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.mute = ARCAM_AV_MUTE_ON + *value;
			if (arcam_av->global->zone1.mute == arcam_av->local.zone1.mute)
				return 0;

			volume = arcam_av->global->zone1.volume;
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.mute = ARCAM_AV_MUTE_ON + *value;
			if (arcam_av->global->zone2.mute == arcam_av->local.zone2.mute)
				return 0;

			volume = arcam_av->global->zone2.volume;
			break;
		}

		if (*value)
			arcam_av_send(arcam_av->port_fd, ARCAM_AV_VOLUME_SET, arcam_av->zone, volume);
		break;

	default:
		return -EINVAL;
	}

	if (!arcam_av_send(arcam_av->port_fd, key, arcam_av->zone, '0' + *value))
		return 1;
	else
		return -1;
}

static int arcam_av_write_enumerated(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, unsigned int *item)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;
	unsigned char code;

	switch(key) {
	case ARCAM_AV_SOURCE:
		if (*item >= ARRAY_SIZE(arcam_av_sources))
			return -EINVAL;

		code = arcam_av_sources[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.source = code;
			if (arcam_av->global->zone1.source == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			arcam_av->local.zone2.source = code;
			if (arcam_av->global->zone2.source == code)
				return 0;
			break;
		}
		break;

	case ARCAM_AV_SOURCE_TYPE:
		if (*item >= ARRAY_SIZE(arcam_av_source_types))
			return -EINVAL;

		code = arcam_av_source_types[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.source_type = code;
			if (arcam_av->global->zone1.source_type == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_DIRECT:
		if (*item >= ARRAY_SIZE(arcam_av_direct))
			return -EINVAL;

		code = arcam_av_direct[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.direct = code;
			if (arcam_av->global->zone1.direct == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_STEREO_DECODE:
		if (*item >= ARRAY_SIZE(arcam_av_stereo_decode_modes))
			return -EINVAL;

		code = arcam_av_stereo_decode_modes[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.stereo_decode = code;
			if (arcam_av->global->zone1.stereo_decode == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_STEREO_EFFECT:
		if (*item >= ARRAY_SIZE(arcam_av_stereo_effects))
			return -EINVAL;

		code = arcam_av_stereo_effects[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.stereo_effect = code;
			if (arcam_av->global->zone1.stereo_effect == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	case ARCAM_AV_MULTI_DECODE:
		if (*item >= ARRAY_SIZE(arcam_av_multi_decode_modes))
			return -EINVAL;

		code = arcam_av_multi_decode_modes[*item].code;

		switch(arcam_av->zone) {
		case ARCAM_AV_ZONE1:
			arcam_av->local.zone1.multi_decode = code;
			if (arcam_av->global->zone1.multi_decode == code)
				return 0;
			break;

		case ARCAM_AV_ZONE2:
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	if (!arcam_av_send(arcam_av->port_fd, key, arcam_av->zone, code))
		return 1;
	else
		return -1;
}


static int arcam_av_read_event(snd_ctl_ext_t *ext, snd_ctl_elem_id_t *id, unsigned int *event_mask)
{
	snd_ctl_arcam_av_t *arcam_av = ext->private_data;
	unsigned int elem;
	int result = 0;

	switch(arcam_av->zone) {
	case ARCAM_AV_ZONE1:
		for (elem = 0; elem < ARRAY_SIZE(arcam_av_zone1); ++elem) {
			if (arcam_av->local.zone1.state[elem] != arcam_av->global->zone1.state[elem]) {
				snd_ctl_elem_id_set_name(id, arcam_av_zone1[elem].name);
				snd_ctl_elem_id_set_numid(id, elem + 1);
				arcam_av->local.zone1.state[elem] = arcam_av->global->zone1.state[elem];
				result = 1;
				break;
			}
		}		
		break;

	case ARCAM_AV_ZONE2:
		for (elem = 0; elem < ARRAY_SIZE(arcam_av_zone2); ++elem) {
			if (arcam_av->local.zone2.state[elem] != arcam_av->global->zone2.state[elem]) {
				snd_ctl_elem_id_set_name(id, arcam_av_zone2[elem].name);
				snd_ctl_elem_id_set_numid(id, elem + 1);
				arcam_av->local.zone2.state[elem] = arcam_av->global->zone2.state[elem];
				result = 1;
				break;
			}
		}
		break;
	}

	if (!result) {
		char buf[10];
		if (recv(arcam_av->ext.poll_fd, buf, sizeof(buf), 0) <= 0) {
			close(arcam_av->ext.poll_fd);
			arcam_av->ext.poll_fd = arcam_av_client(arcam_av->port);
			if (arcam_av->ext.poll_fd > 0)
				fcntl(arcam_av->ext.poll_fd, F_SETFL, O_NONBLOCK);
		}

		result = -EAGAIN;
	} else {
		snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
		*event_mask = SND_CTL_EVENT_MASK_VALUE;
	}

	return result;
}


static snd_ctl_ext_callback_t arcam_av_ext_callback = {
	.close = arcam_av_close,
	.elem_count = arcam_av_elem_count,
	.elem_list = arcam_av_elem_list,
	.find_elem = arcam_av_find_elem,
	.get_attribute = arcam_av_get_attribute,
	.get_integer_info = arcam_av_get_integer_info,
	.get_enumerated_info = arcam_av_get_enumerated_info,
	.get_enumerated_name = arcam_av_get_enumerated_name,
	.read_integer = arcam_av_read_integer,
	.read_enumerated = arcam_av_read_enumerated,
	.write_integer = arcam_av_write_integer,
	.write_enumerated = arcam_av_write_enumerated,
	.read_event = arcam_av_read_event,
};


SND_CTL_PLUGIN_DEFINE_FUNC(arcam_av)
{
	snd_config_iterator_t it, next;
	const char *port = "/dev/ttyS0";
	long zone = 1;
	int err;
	snd_ctl_arcam_av_t *arcam_av = NULL;

	snd_config_for_each(it, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(it);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "port") == 0) {
			if (snd_config_get_string(n, &port) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "zone") == 0) {
			if (snd_config_get_integer(n, &zone) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			if (zone < 1 || zone > 2) {
				SNDERR("Invalid value for %s", id);
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (access(port, R_OK | W_OK) < 0) {
		err = -errno;
		goto error;
	}

	arcam_av = calloc(1, sizeof(*arcam_av) + strlen(port) + 1);
	
	if (!arcam_av)
		return -ENOMEM;

	arcam_av->ext.version = SND_CTL_EXT_VERSION;
	arcam_av->ext.card_idx = 0;
	strncpy(arcam_av->ext.id, arcam_av_name, sizeof(arcam_av->ext.id) - 1);
	strncpy(arcam_av->ext.name, arcam_av_name, sizeof(arcam_av->ext.name) - 1);
	strncpy(arcam_av->ext.longname, arcam_av_name, sizeof(arcam_av->ext.longname) - 1);
	strncpy(arcam_av->ext.mixername, arcam_av_name, sizeof(arcam_av->ext.mixername) - 1);
	arcam_av->ext.poll_fd = -1;
	arcam_av->ext.callback = &arcam_av_ext_callback;
	arcam_av->ext.private_data = arcam_av;

	arcam_av->shm_id = -1;
	arcam_av->port_fd = -1;
	arcam_av->port = strcpy((char*)(arcam_av + 1), port);
	arcam_av->zone = zone != 2 ? ARCAM_AV_ZONE1 : ARCAM_AV_ZONE2;

	arcam_av->port_fd = arcam_av_connect(arcam_av->port);
	if (arcam_av->port_fd < 0) {
		err = -errno;
		goto error;
	}

	if (arcam_av_server_start(&arcam_av->server, arcam_av->port)) {
		err = -errno;
		goto error;
	}

	arcam_av->ext.poll_fd = arcam_av_client(arcam_av->port);
	if (arcam_av->ext.poll_fd < 0) {
		err = -errno;
		goto error;
	}

	fcntl(arcam_av->ext.poll_fd, F_SETFL, O_NONBLOCK);

	arcam_av->global = arcam_av_state_attach(arcam_av->port);
	if (!arcam_av->global) {
		err = -errno;
		goto error;
	}

	err = snd_ctl_ext_create(&arcam_av->ext, name, mode);
	if (err < 0)
		goto error;

	*handlep = arcam_av->ext.handle;
	return 0;

 error:
	perror("arcam_av()");
	arcam_av_close(&arcam_av->ext);
	return err;
}

SND_CTL_PLUGIN_SYMBOL(arcam_av);
