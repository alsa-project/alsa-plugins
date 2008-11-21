/**
 * @file dsp-ctl.c
 * @brief CTL External plugin implementation.
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
#include <stdio.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <alsa/control_external.h>
#include <string.h>
#include "list.h"
#include "debug.h"
#include "dsp-protocol.h"
#include "constants.h"

#define PLAYBACK_VOLUME_CONTROL_NAME 	"PCM Playback Volume"
#define PLAYBACK_MUTE_CONTROL_NAME 	"PCM Playback Switch"
#define RECORDING_CONTROL_NAME		"Capture Switch"

/**
 * data structure to represent a dsp task device node.
 */
typedef struct {
	dsp_protocol_t *dsp_protocol;
	char *name;
	int channels;
	struct list_head list;
} control_list_t;

/**
 * data structure to represent this plugin information.
 */ 
typedef struct snd_ctl_dsp {
	snd_ctl_ext_t ext;
	int num_playbacks;
	int num_recordings;
	control_list_t **controls;
	control_list_t playback_devices;
	control_list_t recording_devices;
} snd_ctl_dsp_t;

static snd_ctl_dsp_t *free_ref;
/**
 * @param control_list control list to be freed.
 *
 * It passes through this control list and frees
 * all its nodes.
 * 
 * @return zero. success.
 */ 
static int free_control_list(control_list_t * control_list)
{
	struct list_head *pos, *q;
	control_list_t *tmp;
	list_for_each_safe(pos, q, &control_list->list) {
		tmp = list_entry(pos, control_list_t, list);
		list_del(pos);
		free(tmp->name);
		close(tmp->dsp_protocol->fd);
		dsp_protocol_destroy(&(tmp->dsp_protocol));
		free(tmp);
	}
	return 0;
}

/**
 * @param ext snd_ctl_ext_t structure.
 *
 * It is the close event handler for this plugin.
 * It frees all the allocated memory.
 * 
 * @return zero. success.
 */ 
static void dsp_ctl_close(snd_ctl_ext_t * ext)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	DENTER();
	free(dsp_ctl->controls);
	free_control_list(&(dsp_ctl->playback_devices));
	free_control_list(&(dsp_ctl->recording_devices));
//      free(dsp_ctl);
	DLEAVE(0);
}

/**
 * @param ext snd_ctl_ext_t structure.
 *
 * It returns number of controls to be used by this
 * plugin. It is based on number of recording and playback
 * device nodes configured to be handled by this plugin.
 * 
 * @return number of controls.
 */ 
static int dsp_ctl_elem_count(snd_ctl_ext_t * ext)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	int ret = 2 * dsp_ctl->num_playbacks + dsp_ctl->num_recordings;
	DENTER();
	DLEAVE(ret);
	return ret;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param offset offset of current control.
 * @param id id of current control element.
 *
 * It sets name and index for a control based
 * of its offset.
 * 
 * @return zero. success.
 */ 
static int dsp_ctl_elem_list(snd_ctl_ext_t * ext, unsigned int offset,
			     snd_ctl_elem_id_t * id)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	int ret = 0;

	DENTER();
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	if (offset < 2 * dsp_ctl->num_playbacks) {
		if (offset % 2 == 1)
			snd_ctl_elem_id_set_name(id,
						 PLAYBACK_MUTE_CONTROL_NAME);
		else
			snd_ctl_elem_id_set_name(id,
						 PLAYBACK_VOLUME_CONTROL_NAME);
		offset /= 2;
	} else {
		offset -= 2 * dsp_ctl->num_playbacks;
		snd_ctl_elem_id_set_name(id, RECORDING_CONTROL_NAME);
	}
	snd_ctl_elem_id_set_index(id, offset);
	DLEAVE(ret);
	return ret;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param id control element id from alsa-lib
 *
 * It searches for an control element using 
 * its name and index. If this control can
 * be found, it returns a key to represent it
 * for future use. This key is an index of an
 * array of controls whose is composed of three
 * types of elements: PCM Volume, PCM Switch and
 * Capture Switch. This array is organized as 
 * follows:
 * pv0, ps0, pv1, ps1, ..., pv_n, ps_n, cs0, cs1, ..., cs_m
 *
 * where, pvi is the i-th PCM Volume Control
 *        psi is the i-th PCM Switch Control
 *        csi is the i-th Capture Switch Control
 *        n - is the number of Playback devices
 *        m - is the number of Recording devices
 *
 * @return if control is not found, returns SND_CTL_EXT_KEY_NOT_FOUND.
 * Otherwise, returns a key of control.
 */
static snd_ctl_ext_key_t dsp_ctl_find_elem(snd_ctl_ext_t * ext,
					   const snd_ctl_elem_id_t * id)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	snd_ctl_ext_key_t ret = SND_CTL_EXT_KEY_NOT_FOUND;
	int idx;
	const char *name;

	DENTER();
	idx = snd_ctl_elem_id_get_index(id);
	name = snd_ctl_elem_id_get_name(id);
	if (strcmp(PLAYBACK_VOLUME_CONTROL_NAME, name) == 0)
		ret = idx * 2;
	else if (strcmp(PLAYBACK_MUTE_CONTROL_NAME, name) == 0)
		ret = idx * 2 + 1;
	else
		ret = 2 * dsp_ctl->num_playbacks + idx;
	if (ret == SND_CTL_EXT_KEY_NOT_FOUND)
		DPRINT("Not Found name %s, index %d\n",
		       snd_ctl_elem_id_get_name(id), idx);
	DLEAVE((int)ret);
	return ret;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param key current control key to be handled.
 * @param type type of this control.
 * @param acc access type of this control.
 * @param count number of channels of this control.
 *
 * It provides information about a control. 
 * Playback device node has an integer and a boolean
 * control. Recording has only boolean control.
 *
 * @return zero. success.
 */
static int dsp_ctl_get_attribute(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
				 int *type, unsigned int *acc,
				 unsigned int *count)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	int ret = 0;
	DENTER();

	if (key >= 2 * dsp_ctl->num_playbacks || key % 2 == 1)
		*type = SND_CTL_ELEM_TYPE_BOOLEAN;
	else
		*type = SND_CTL_ELEM_TYPE_INTEGER;

	*count = dsp_ctl->controls[key]->channels;
	*acc = SND_CTL_EXT_ACCESS_READWRITE;
	DLEAVE(ret);
	return ret;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param key current control key to be handled.
 * @param imin minimum value of this integer control.
 * @param imax maximum value of this integer control.
 * @param istep steps value of this integer control.
 *
 * It provides information about integer control. It
 * consideres both boolean and integer controls.
 *
 * @return  zero. success.
 */
static int dsp_ctl_get_integer_info(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
				    long *imin, long *imax, long *istep)
{
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	DENTER();
	*imin = 0;
	if (key >= 2 * dsp_ctl->num_playbacks || key % 2 == 1)
		*imax = 1;
	else
		*imax = 100;
	*istep = 0;
	DLEAVE(0);
	return 0;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param key current control key to be handled.
 * @param value return value holder.
 *
 * It reads volume information from dsp task node and 
 * fills it into value to alsa-lib.
 *
 * @return zero. success.
 */
static int dsp_ctl_read_integer(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
				long *value)
{
	int ret = 0;
	unsigned char left, right;
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	control_list_t *tmp = dsp_ctl->controls[key];

	DENTER();
	if (key >= 2 * dsp_ctl->num_playbacks || key % 2 == 1) {
		ret = dsp_protocol_get_mute(tmp->dsp_protocol);
		if (ret >= 0) {
			left = ret == 0 ? 1 : 0;
			right = left;
		}
	} else
		ret = dsp_protocol_get_volume(tmp->dsp_protocol, &left, &right);
	if (ret >= 0) {
		value[0] = left;
		if (tmp->channels == 2)
			value[1] = right;
	} else {
		value[0] = 0;
		if (tmp->channels == 2)
			value[1] = 0;
		ret = 0;
	}

	DLEAVE(ret);
	return ret;
}

/**
 * @param ext snd_ctl_ext_t structure.
 * @param key current control key to be handled.
 * @param value volume value to be written.
 *
 * It writes volume information to dsp task node from
 * value that comes from alsa-lib. It checks if value
 * coming from alsa-lib is different of value in dsp 
 * side.
 *
 * @return zero if not updated. one if updated.
 */
static int dsp_ctl_write_integer(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
				 long *value)
{
	int ret;
	unsigned char left, right;
	snd_ctl_dsp_t *dsp_ctl = ext->private_data;
	control_list_t *tmp = dsp_ctl->controls[key];

	DENTER();
	if (key >= 2 * dsp_ctl->num_playbacks || key % 2 == 1) {
		if ((ret = dsp_protocol_get_mute(tmp->dsp_protocol)) < 0)
			goto zero;
		left = ret == 0 ? 1 : 0;
		right = left;
	} else
	    if ((ret =
		 dsp_protocol_get_volume(tmp->dsp_protocol, &left, &right)) < 0)
		goto zero;

	if (tmp->channels == 2) {
		if (left == value[0] && right == value[1]) {
			ret = 0;
			goto out;
		}
		right = value[1];
		left = value[0];
	} else {
		if (left == value[0]) {
			ret = 0;
			goto out;
		}
		right = left = value[0];
	}

	if (key >= 2 * dsp_ctl->num_playbacks || key % 2 == 1) {
		if ((ret =
		     dsp_protocol_set_mute(tmp->dsp_protocol,
					   left == 0 ? 1 : 0)) < 0)
			goto zero;
	} else
	    if ((ret =
		 dsp_protocol_set_volume(tmp->dsp_protocol, left, right)) < 0)
		goto zero;
	ret = 1;
	goto out;
      zero:
	value[0] = 0;
	if (tmp->channels == 2)
		value[1] = 0;
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param ext
 * @param id
 * @param event_mask
 *
 * @return -EIO
 */
static int dsp_ctl_read_event(snd_ctl_ext_t * ext, snd_ctl_elem_id_t * id,
			      unsigned int *event_mask)
{
	return -EIO;
}

/**
 * @param n configuration file parse tree. 
 * @param device_list list of device files to be filled.
 *
 * It searches for device file names in given configuration parse
 * tree. When one device file name is found, it is filled into device_list.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int fill_control_list(snd_config_t * n, control_list_t * control_list)
{
	snd_config_iterator_t j, nextj;
	control_list_t *tmp;
	long idx = 0;
	DENTER();
	INIT_LIST_HEAD(&control_list->list);
	snd_config_for_each(j, nextj, n) {
		snd_config_t *s = snd_config_iterator_entry(j);
		const char *id_number;
		long k;
		if (snd_config_get_id(s, &id_number) < 0)
			continue;
		if (safe_strtol(id_number, &k) < 0) {
			SNDERR("id of field %s is not an integer", id_number);
			idx = -EINVAL;
			goto out;
		}
		if (k == idx) {
			idx++;
			/* add to available dsp task nodes */
			tmp = (control_list_t *) malloc(sizeof(control_list_t));
			if (snd_config_get_ascii(s, &(tmp->name)) < 0) {
				SNDERR("invalid ascii string for id %s\n",
				       id_number);
				idx = -EINVAL;
				goto out;
			}
			list_add(&(tmp->list), &(control_list->list));
		}
	}
      out:
	DLEAVE((int)idx);
	return idx;
}

/**
 * @param dsp_ctl snd_dsp_t structure.
 *
 * It probes all dsp tasks configured to be handled by this
 * plugin. It gets all needed information about volume controling.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int dsp_ctl_probe_dsp_task(snd_ctl_dsp_t * dsp_ctl)
{
	int err = 0, i;
	control_list_t *tmp;
	struct list_head *lists[2] =
	    { &(dsp_ctl->playback_devices.list),
&(dsp_ctl->recording_devices.list) };
	DENTER();
	DPRINT("Probing dsp device nodes \n");
	for (i = 0; i < 2; i++) {
		list_for_each_entry(tmp, lists[i], list) {
			DPRINT("Trying to use %s\n", tmp->name);
			/* Initialise the dsp_protocol and create connection */
			if ((err =
			     dsp_protocol_create(&(tmp->dsp_protocol))) < 0)
				goto out;
			if ((tmp->channels =
			     dsp_protocol_probe_node(tmp->dsp_protocol,
						     tmp->name)) < 0) {
				DPRINT("%s is not available now\n", tmp->name);
				err = tmp->channels;
				close(tmp->dsp_protocol->fd);	//memory leak?!?
				goto out;
			}
		}
	}
	if (err < 0) {
		DPRINT("No valid dsp task nodes for now. Exiting.\n");
	}
      out:
	DLEAVE(err);
	return err;
}

/**
 * @param dsp_ctl snd_dsp_t structure.
 *
 * It fills an array of controls to represent PCM Volume,
 * PCM Switch and Capture Switch controls.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int dsp_ctl_fill_controls(snd_ctl_dsp_t * dsp_ctl)
{
	int ret = 0;
	int i;
	int num_controls = 2 * dsp_ctl->num_playbacks + dsp_ctl->num_recordings;
	DENTER();
	control_list_t *tmp;
	dsp_ctl->controls = calloc(num_controls, sizeof(control_list_t *));
	if (dsp_ctl->controls == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	i = 0;
	/* Each pcm task node should have a PCM Volume and PCM Switch control */
	list_for_each_entry(tmp, &(dsp_ctl->playback_devices.list), list) {
		dsp_ctl->controls[i] = tmp;
		dsp_ctl->controls[i + 1] = tmp;
		i += 2;
	}
	/* Each pcm_rec node should have a Capture Switch control */
	list_for_each_entry(tmp, &(dsp_ctl->recording_devices.list), list)
	    dsp_ctl->controls[i++] = tmp;

      out:
	DLEAVE(ret);
	return ret;
}

/**
 * Alsa-lib callback structure.
 */
static snd_ctl_ext_callback_t dsp_ctl_ext_callback = {
	.close = dsp_ctl_close,
	.elem_count = dsp_ctl_elem_count,
	.elem_list = dsp_ctl_elem_list,
	.find_elem = dsp_ctl_find_elem,
	.get_attribute = dsp_ctl_get_attribute,
	.get_integer_info = dsp_ctl_get_integer_info,
	.read_integer = dsp_ctl_read_integer,
	.write_integer = dsp_ctl_write_integer,
	.read_event = dsp_ctl_read_event,
};

/**
 * It initializes the alsa ctl plugin. It reads the parameters and creates the 
 * connection with the pcm device file.
 *
 * @return zero if success, otherwise a negative error code.
 */
SND_CTL_PLUGIN_DEFINE_FUNC(dsp_ctl)
{
	snd_config_iterator_t i, next;
	snd_ctl_dsp_t *dsp_ctl;
	int err;
	int ret;

	DENTER();
	/* Allocate the structure */
	dsp_ctl = calloc(1, sizeof(*dsp_ctl));
	if (dsp_ctl == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	/* Read the configuration searching for configurated devices */
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "playback_devices") == 0) {
			if (snd_config_get_type(n) == SND_CONFIG_TYPE_COMPOUND){
				if ((dsp_ctl->num_playbacks =
				     fill_control_list(n,
						&(dsp_ctl->
						playback_devices))) < 0) {
					SNDERR("Could not fill control"
						" list for playback devices\n");
					err = -EINVAL;
					goto error;
				}
			} else {
				SNDERR("Invalid type for %s", id);
				err = -EINVAL;
				goto error;
			}
			continue;
		}
		if (strcmp(id, "recording_devices") == 0) {
			if (snd_config_get_type(n) == SND_CONFIG_TYPE_COMPOUND){
				if ((dsp_ctl->num_recordings =
				     fill_control_list(n,
						&(dsp_ctl->
						recording_devices))) < 0) {
					SNDERR("Could not fill string "
						"list for recording devices\n");
					err = -EINVAL;
					goto error;
				}
			} else {
				SNDERR("Invalid type for %s", id);
				err = -EINVAL;
				goto error;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		err = -EINVAL;
		goto error;
	}

	if ((err = dsp_ctl_probe_dsp_task(dsp_ctl)) < 0)
		goto error;

	if ((err = dsp_ctl_fill_controls(dsp_ctl)) < 0)
		goto error;
	dsp_ctl->ext.version = SND_CTL_EXT_VERSION;
	dsp_ctl->ext.card_idx = 0;	/* FIXME */
	strcpy(dsp_ctl->ext.id, "ALSA-DSP-CTL");
	strcpy(dsp_ctl->ext.name, "Alsa-DSP external ctl plugin");
	strcpy(dsp_ctl->ext.longname, "External Control Alsa plugin for DSP");
	strcpy(dsp_ctl->ext.mixername, "ALSA-DSP plugin Mixer");
	dsp_ctl->ext.callback = &dsp_ctl_ext_callback;
	dsp_ctl->ext.private_data = dsp_ctl;
	free_ref = dsp_ctl;

	if ((err = snd_ctl_ext_create(&dsp_ctl->ext, name, mode)) < 0)
		goto error;

	*handlep = dsp_ctl->ext.handle;
	ret = 0;
	goto out;
      error:
	ret = err;
	free(dsp_ctl);
      out:
	DLEAVE(ret);
	return ret;
}
static void dsp_ctl_descructor(void) __attribute__ ((destructor));

static void dsp_ctl_descructor(void)
{
	DENTER();
	DPRINT("dsp ctl destructor\n");
	DPRINT("checking for memories leaks and releasing resources\n");
	if (free_ref) {
		if (free_ref->controls) 
			free(free_ref->controls);

		free_control_list(&(free_ref->playback_devices));

		free_control_list(&(free_ref->recording_devices));
		
		free(free_ref);
		free_ref = NULL;
	}
	DLEAVE(0);

}

SND_CTL_PLUGIN_SYMBOL(dsp_ctl);
