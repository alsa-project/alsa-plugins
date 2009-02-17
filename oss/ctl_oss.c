/*
 * ALSA <-> OSS mixer control plugin
 *
 * Copyright (c) 2005 by Takashi Iwai <tiwai@suse.de>
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

/*
 * TODO: implement the pseudo poll with thread (and pipe as pollfd)?
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <alsa/control_external.h>
#include <linux/soundcard.h>

typedef struct snd_ctl_oss {
	snd_ctl_ext_t ext;
	char *device;
	int fd;
	int exclusive_input;
	int stereo_mask;
	unsigned int num_vol_ctls;
	unsigned int vol_ctl[SOUND_MIXER_NRDEVICES];
	unsigned int num_rec_items;
	unsigned int rec_item[SOUND_MIXER_NRDEVICES];
} snd_ctl_oss_t;

static const char *const vol_devices[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME] =	"Master Playback Volume",
	[SOUND_MIXER_BASS] =	"Tone Control - Bass",
	[SOUND_MIXER_TREBLE] =	"Tone Control - Treble",
	[SOUND_MIXER_SYNTH] =	"Synth Playback Volume",
	[SOUND_MIXER_PCM] =	"PCM Playback Volume",
	[SOUND_MIXER_SPEAKER] =	"PC Speaker Playback Volume",
	[SOUND_MIXER_LINE] =	"Line Playback Volume",
	[SOUND_MIXER_MIC] =	"Mic Playback Volume",
	[SOUND_MIXER_CD] =	"CD Playback Volume",
	[SOUND_MIXER_IMIX] =	"Monitor Mix Playback Volume",
	[SOUND_MIXER_ALTPCM] =	"Headphone Playback Volume",
	[SOUND_MIXER_RECLEV] =	"Capture Volume",
	[SOUND_MIXER_IGAIN] =	"Capture Volume",
	[SOUND_MIXER_OGAIN] =	"Playback Volume",
	[SOUND_MIXER_LINE1] =	"Aux Playback Volume",
	[SOUND_MIXER_LINE2] =	"Aux1 Playback Volume",
	[SOUND_MIXER_LINE3] =	"Line1 Playback Volume",
	[SOUND_MIXER_DIGITAL1] = "IEC958 Playback Volume",
	[SOUND_MIXER_DIGITAL2] = "Digital Playback Volume",
	[SOUND_MIXER_DIGITAL3] = "Digital1 Playback Volume",
	[SOUND_MIXER_PHONEIN] =	"Phone Playback Volume",
	[SOUND_MIXER_PHONEOUT] = "Master Mono Playback Volume",
	[SOUND_MIXER_VIDEO] =	"Video Playback Volume",
	[SOUND_MIXER_RADIO] =	"Radio Playback Volume",
	[SOUND_MIXER_MONITOR] =	"Monitor Playback Volume",
};

static const char *const rec_devices[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME] =	"Mix Capture Switch",
	[SOUND_MIXER_SYNTH] =	"Synth Capture Switch",
	[SOUND_MIXER_PCM] =	"PCM Capture Switch",
	[SOUND_MIXER_LINE] =	"Line Capture Switch",
	[SOUND_MIXER_MIC] =	"Mic Capture Switch",
	[SOUND_MIXER_CD] =	"CD Capture Switch",
	[SOUND_MIXER_LINE1] =	"Aux Capture Switch",
	[SOUND_MIXER_LINE2] =	"Aux1 Capture Switch",
	[SOUND_MIXER_LINE3] =	"Line1 Capture Switch",
	[SOUND_MIXER_DIGITAL1] = "IEC958 Capture Switch",
	[SOUND_MIXER_DIGITAL2] = "Digital Capture Switch",
	[SOUND_MIXER_DIGITAL3] = "Digital1 Capture Switch",
	[SOUND_MIXER_PHONEIN] =	"Phone Capture Switch",
	[SOUND_MIXER_VIDEO] =	"Video Capture Switch",
	[SOUND_MIXER_RADIO] =	"Radio Capture Switch",
};	

static const char *const rec_items[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME] =	"Mix",
	[SOUND_MIXER_SYNTH] =	"Synth",
	[SOUND_MIXER_PCM] =	"PCM",
	[SOUND_MIXER_LINE] =	"Line",
	[SOUND_MIXER_MIC] =	"Mic",
	[SOUND_MIXER_CD] =	"CD",
	[SOUND_MIXER_LINE1] =	"Aux",
	[SOUND_MIXER_LINE2] =	"Aux1",
	[SOUND_MIXER_LINE3] =	"Line1",
	[SOUND_MIXER_DIGITAL1] = "IEC958",
	[SOUND_MIXER_DIGITAL2] = "Digital",
	[SOUND_MIXER_DIGITAL3] = "Digital1",
	[SOUND_MIXER_PHONEIN] =	"Phone",
	[SOUND_MIXER_VIDEO] =	"Video",
	[SOUND_MIXER_RADIO] =	"Radio",
};	

static void oss_close(snd_ctl_ext_t *ext)
{
	snd_ctl_oss_t *oss = ext->private_data;

	close(oss->fd);
	free(oss->device);
	free(oss);
}

static int oss_elem_count(snd_ctl_ext_t *ext)
{
	snd_ctl_oss_t *oss = ext->private_data;
	int num;

	num = oss->num_vol_ctls;
	if (oss->exclusive_input)
		num++;
	else if (oss->num_rec_items)
		num += oss->num_rec_items;
	return num;
}

static int oss_elem_list(snd_ctl_ext_t *ext, unsigned int offset, snd_ctl_elem_id_t *id)
{
	snd_ctl_oss_t *oss = ext->private_data;

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	if (offset < oss->num_vol_ctls)
		snd_ctl_elem_id_set_name(id, vol_devices[oss->vol_ctl[offset]]);
	else if (oss->exclusive_input)
		snd_ctl_elem_id_set_name(id, "Capture Source");
	else {
		offset -= oss->num_vol_ctls;
		snd_ctl_elem_id_set_name(id, rec_devices[oss->rec_item[offset]]);
	}
	return 0;
}

#define OSS_KEY_DEVICE_MASK	0x1f
#define OSS_KEY_CAPTURE_FLAG	(1 << 8)
#define OSS_KEY_CAPTURE_MUX	(1 << 16)

static snd_ctl_ext_key_t oss_find_elem(snd_ctl_ext_t *ext,
				       const snd_ctl_elem_id_t *id)
{
	snd_ctl_oss_t *oss = ext->private_data;
	const char *name;
	unsigned int i, key, numid;

	numid = snd_ctl_elem_id_get_numid(id);
	if (numid > 0) {
		numid--;
		if (numid < oss->num_vol_ctls)
			return oss->vol_ctl[numid];
		numid -= oss->num_vol_ctls;
		if (oss->exclusive_input) {
			if (!numid)
				return OSS_KEY_CAPTURE_MUX;
		} else if (numid < oss->num_rec_items)
			return oss->rec_item[numid] |
				OSS_KEY_CAPTURE_FLAG;
	}

	name = snd_ctl_elem_id_get_name(id);
	if (! strcmp(name, "Capture Source")) {
		if (oss->exclusive_input)
			return OSS_KEY_CAPTURE_MUX;
		else
			return SND_CTL_EXT_KEY_NOT_FOUND;
	}
	for (i = 0; i < oss->num_vol_ctls; i++) {
		key = oss->vol_ctl[i];
		if (! strcmp(name, vol_devices[key]))
			return key;
	}
	for (i = 0; i < oss->num_rec_items; i++) {
		key = oss->rec_item[i];
		if (! strcmp(name, rec_devices[key]))
			return key | OSS_KEY_CAPTURE_FLAG;
	}
	return SND_CTL_EXT_KEY_NOT_FOUND;
}

static int oss_get_attribute(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
			     int *type, unsigned int *acc, unsigned int *count)
{
	snd_ctl_oss_t *oss = ext->private_data;

	if (key == OSS_KEY_CAPTURE_MUX) {
		*type = SND_CTL_ELEM_TYPE_ENUMERATED;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
	} else if (key & OSS_KEY_CAPTURE_FLAG) {
		*type = SND_CTL_ELEM_TYPE_BOOLEAN;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		*count = 1;
	} else {
		*type = SND_CTL_ELEM_TYPE_INTEGER;
		*acc = SND_CTL_EXT_ACCESS_READWRITE;
		if (oss->stereo_mask & (1 << key))
			*count = 2;
		else
			*count = 1;
	}
	return 0;
}

static int oss_get_integer_info(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
				snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
				long *imin, long *imax, long *istep)
{
	*istep = 0;
	*imin = 0;
	*imax = 100;
	return 0;
}

static int oss_get_enumerated_info(snd_ctl_ext_t *ext,
				   snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
				   unsigned int *items)
{
	snd_ctl_oss_t *oss = ext->private_data;

	*items = oss->num_rec_items;
	return 0;
}

static int oss_get_enumerated_name(snd_ctl_ext_t *ext,
				   snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
				   unsigned int item, char *name,
				   size_t name_max_len)
{
	snd_ctl_oss_t *oss = ext->private_data;

	if (item >= oss->num_rec_items)
		return -EINVAL;
	item = oss->rec_item[item];
	strncpy(name, rec_items[item], name_max_len - 1);
	name[name_max_len - 1] = 0;
	return 0;
}

static int oss_read_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	snd_ctl_oss_t *oss = ext->private_data;
	int val;

	if (key & OSS_KEY_CAPTURE_FLAG) {
		key &= OSS_KEY_DEVICE_MASK;
		if (ioctl(oss->fd, SOUND_MIXER_READ_RECSRC, &val) < 0)
			return -errno;
		*value = (val & (1 << key)) ? 1 : 0;
	} else {
		if (ioctl(oss->fd, MIXER_READ(key), &val) < 0)
			return -errno;
		*value = val & 0xff;
		if (oss->stereo_mask & (1 << key))
			value[1] = (val >> 8) & 0xff;
	}
	return 0;
}

static int oss_read_enumerated(snd_ctl_ext_t *ext,
			       snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
			       unsigned int *items)
{
	snd_ctl_oss_t *oss = ext->private_data;
	unsigned int i, val;

	*items = 0;
	if (ioctl(oss->fd, SOUND_MIXER_READ_RECSRC, &val) < 0)
		return -errno;
	for (i = 0; i < oss->num_rec_items; i++) {
		if (val & (1 << oss->rec_item[i])) {
			*items = i;
			break;
		}
	}
	return 0;
}

static int oss_write_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	snd_ctl_oss_t *oss = ext->private_data;
	int val, oval;

	if (key & OSS_KEY_CAPTURE_FLAG) {
		key &= OSS_KEY_DEVICE_MASK;
		if (ioctl(oss->fd, SOUND_MIXER_READ_RECSRC, &oval) < 0)
			return -errno;
		if (*value)
			val = oval | (1 << key);
		else
			val = oval & ~(1 << key);
		if (oval == val)
			return 0;
		if (ioctl(oss->fd, SOUND_MIXER_WRITE_RECSRC, &val) < 0)
			return -errno;
		return 1;
	} else {
		val = *value;
		if (oss->stereo_mask & (1 << key))
			val |= value[1] << 8;
		if (ioctl(oss->fd, MIXER_READ(key), &oval) < 0)
			return -errno;
		if (oval == val)
			return 0;
		if (ioctl(oss->fd, MIXER_WRITE(key), &val) < 0)
			return -errno;
		return 1;
	}
}

static int oss_write_enumerated(snd_ctl_ext_t *ext,
				snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
				unsigned int *items)
{
	snd_ctl_oss_t *oss = ext->private_data;
	int val, oval;

	if (ioctl(oss->fd, SOUND_MIXER_READ_RECSRC, &oval) < 0)
		return -errno;
	val = 1 << oss->rec_item[*items];
	if (val == oval)
		return 0;
	if (ioctl(oss->fd, SOUND_MIXER_WRITE_RECSRC, &val) < 0)
		return -errno;
	return 1;
}

static int oss_read_event(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
			  snd_ctl_elem_id_t *id ATTRIBUTE_UNUSED,
			  unsigned int *event_mask ATTRIBUTE_UNUSED)
{
	return -EAGAIN;
}

static snd_ctl_ext_callback_t oss_ext_callback = {
	.close = oss_close,
	.elem_count = oss_elem_count,
	.elem_list = oss_elem_list,
	.find_elem = oss_find_elem,
	.get_attribute = oss_get_attribute,
	.get_integer_info = oss_get_integer_info,
	.get_enumerated_info = oss_get_enumerated_info,
	.get_enumerated_name = oss_get_enumerated_name,
	.read_integer = oss_read_integer,
	.read_enumerated = oss_read_enumerated,
	.write_integer = oss_write_integer,
	.write_enumerated = oss_write_enumerated,
	.read_event = oss_read_event,
};


SND_CTL_PLUGIN_DEFINE_FUNC(oss)
{
	snd_config_iterator_t it, next;
	const char *device = "/dev/mixer";
	struct mixer_info mixinfo;
	int i, err, val;
	snd_ctl_oss_t *oss;
	
	snd_config_for_each(it, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(it);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "device") == 0) {
			if (snd_config_get_string(n, &device) < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	oss = calloc(1, sizeof(*oss));
	oss->device = strdup(device);
	oss->fd = -1;
	if (oss->device == NULL) {
		SNDERR("cannot allocate");
		free(oss);
		return -ENOMEM;
	}
	oss->fd = open(device, O_RDWR);
	if (oss->fd < 0) {
		err = -errno;
		SNDERR("Cannot open device %s", device);
		goto error;
	}

	if (ioctl(oss->fd, SOUND_MIXER_INFO, &mixinfo) < 0) {
		err = -errno;
		SNDERR("Cannot get mixer info for device %s", device);
		goto error;
	}

	oss->ext.version = SND_CTL_EXT_VERSION;
	oss->ext.card_idx = 0; /* FIXME */
	strncpy(oss->ext.id, mixinfo.id, sizeof(oss->ext.id) - 1);
	strcpy(oss->ext.driver, "OSS-Emulation");
	strncpy(oss->ext.name, mixinfo.name, sizeof(oss->ext.name) - 1);
	strncpy(oss->ext.longname, mixinfo.name, sizeof(oss->ext.longname) - 1);
	strncpy(oss->ext.mixername, mixinfo.name, sizeof(oss->ext.mixername) - 1);
	oss->ext.poll_fd = -1;
	oss->ext.callback = &oss_ext_callback;
	oss->ext.private_data = oss;

	oss->num_vol_ctls = 0;
	val = 0;
	if (ioctl(oss->fd, SOUND_MIXER_READ_DEVMASK, &val) < 0)
		perror("ctl_oss: DEVMASK error");
	else {
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if ((val & (1 << i)) && vol_devices[i])
				oss->vol_ctl[oss->num_vol_ctls++] = i;
		}
	}

	if (ioctl(oss->fd, SOUND_MIXER_READ_STEREODEVS, &oss->stereo_mask) < 0)
		perror("ctl_oss: STEREODEVS error");
	val = 0;
	if (ioctl(oss->fd, SOUND_MIXER_READ_CAPS, &val) < 0)
		perror("ctl_oss: MIXER_CAPS error");
	else if (val & SOUND_CAP_EXCL_INPUT)
		oss->exclusive_input = 1;

	oss->num_rec_items = 0;
	val = 0;
	if (ioctl(oss->fd, SOUND_MIXER_READ_RECMASK, &val) < 0)
		perror("ctl_oss: MIXER_RECMASK error");
	else {
		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (val & (1 << i)) {
				if (oss->exclusive_input) {
					if (! rec_items[i])
						continue;
				} else {
					if (! rec_devices[i])
						continue;
				}
				oss->rec_item[oss->num_rec_items++] = i;
			}
		}
	}
	if (! oss->num_rec_items)
		oss->exclusive_input = 0;

	err = snd_ctl_ext_create(&oss->ext, name, mode);
	if (err < 0)
		goto error;

	*handlep = oss->ext.handle;
	return 0;

 error:
	if (oss->fd >= 0)
		close(oss->fd);
	free(oss->device);
	free(oss);
	return err;
}

SND_CTL_PLUGIN_SYMBOL(oss);
