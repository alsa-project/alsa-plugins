/**
 * @file dsp-protocol.c
 * @brief Protocol to communicate with DSP side.
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
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/sem.h>

#include "dsp-protocol.h"
#include "constants.h"
#include "debug.h"
#include "types.h"
#include "reporting.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef USE_RESOURCE_MANAGER
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#define AUDIO_PM_SERVICE            "com.nokia.osso_audio_pm"
#define AUDIO_PM_PLAYBACK_RESOURCE  "/com/nokia/osso/pm/audio/playback"
#define AUDIO_PM_RECORD_RESOURCE    "/com/nokia/osso/pm/audio/record"
#define RESOURCE_INTERFACE          "com.nokia.osso_resource_manager"
#define RESOURCE_TIMEOUT            200
#endif /* USE_RESOURCE_MANAGER */

#define MAGIC_NUMBER		0x00A3D70A
#define PANNING_STEP		0x06
#define TASK_IOCTL_LOCK         0x10002
#define TASK_IOCTL_UNLOCK       0x10003
/* internal datatypes declarations */
union semun {
	int val;		/* value for SETVAL */
	struct semid_ds *buf;	/* buffer for IPC_STAT & IPC_SET */
	u_short *array;		/* array for GETALL & SETALL */
};
/* internal features declarations */
static int dsp_protocol_flush(dsp_protocol_t * dsp_protocol);
static int dsp_protocol_send_command(dsp_protocol_t * dsp_protocol,
				     const short int command);
static void dsp_protocol_linear2Q15(const unsigned short int input,
				    unsigned short int *scale, 
				    unsigned short int *power2);
static void dsp_protocol_Q152linear(const unsigned short int scale,
				    const unsigned short int power2, 
				    unsigned short int *output);
static int dsp_protocol_update_state(dsp_protocol_t * dsp_protocol);
static inline int dsp_protocol_get_sem(dsp_protocol_t * dsp_protocol);
static inline int dsp_protocol_lock_dev(dsp_protocol_t * dsp_protocol);
static inline int dsp_protocol_unlock_dev(dsp_protocol_t * dsp_protocol);
/* Initialisation phase features definitions */
/**
 * @param dsp_protocol DSP protocol reference pointer to be instanciated.
 * 
 * Creates new dsp_protocol object instance and initializes it
 * with default parameters. After this the device must be
 * separately opened with gst_dspaudio_open_node() method.
 *
 * @return zero if success, otherwise a negative error code
 *          (-ENOMEM - fail when requesting memory for data structures).
 */
int dsp_protocol_create(dsp_protocol_t ** dsp_protocol)
{
	pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
	int ret = 0;
	DENTER();
	*dsp_protocol = (dsp_protocol_t *) calloc(1, sizeof(dsp_protocol_t));
	if ((*dsp_protocol) == NULL) {
		DERROR("Could not allocate dsp_protocol instance\n");
		ret = -ENOMEM;
		goto out;
	}
	(*dsp_protocol)->fd = -1;
	(*dsp_protocol)->device = NULL;
	(*dsp_protocol)->state = STATE_UNINITIALISED;
	(*dsp_protocol)->mute = 0;
	(*dsp_protocol)->stream_id = 0;
	(*dsp_protocol)->bridge_buffer_size = 0;
	(*dsp_protocol)->mmap_buffer_size = 0;
	(*dsp_protocol)->mmap_buffer = NULL;
	(*dsp_protocol)->mutex = mutex;
	(*dsp_protocol)->sem_set_id = -1;
#ifdef USE_RESOURCE_MANAGER
        (*dsp_protocol)->dbus_connection = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
#endif
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol DSP protocol reference pointer to be initialized.
 * @param device dsp device node name.
 * 
 * Opens pcm dsp device file and initializes dsp_protocol
 * with information about stream, state and mmapbuffer.
 *
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_open_node(dsp_protocol_t * dsp_protocol, const char *device)
{
	int ret;
	short int tmp;
	audio_status_info_t audio_status_info;
	audio_init_status_t audio_init_status;
	DENTER();
	if (dsp_protocol->state != STATE_UNINITIALISED) {
		report_dsp_protocol
		    ("Trying to send open node from a non-valid state",
		     dsp_protocol);
		ret = -EIO;
		goto out;
	}

	dsp_protocol->fd = open(device, O_RDWR);
	if (dsp_protocol->fd < 0) {
		DERROR("Could not open pcm device file %s\n", device);
		ret = errno;
		goto out;
	}
	dsp_protocol->device = strdup(device);
	dsp_protocol_get_sem(dsp_protocol);
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	if ((ret = dsp_protocol_flush(dsp_protocol)) < 0)
		goto unlock;
	tmp = DSP_CMD_STATE;
	if (write(dsp_protocol->fd, &tmp, sizeof(short int)) < 0) {
		ret = -EIO;
		goto unlock;
	}
	if ((ret = read(dsp_protocol->fd, &audio_status_info,
			sizeof(audio_status_info_t))) < 0) {
		report_dsp_protocol("Could not read audio_status_info",
				    dsp_protocol);
		goto unlock;
	}
	report_audio_status_info("Received:", audio_status_info);
	if (audio_status_info.status == STATE_UNINITIALISED) {
		tmp = DSP_CMD_INIT;
		if (write(dsp_protocol->fd, &tmp, sizeof(short int)) < 0) {
			ret = -EIO;
			goto unlock;
		}
		if ((ret = read(dsp_protocol->fd, &audio_init_status,
				sizeof(audio_init_status_t))) < 0) {
			report_dsp_protocol("Error reading INIT status",
					    dsp_protocol);
			goto unlock;
		}
		report_audio_init_status("Received:", audio_init_status);
		/* receive info from audio_init_status */
		dsp_protocol->stream_id = audio_init_status.stream_id;
		dsp_protocol->bridge_buffer_size =
		    audio_init_status.bridge_buffer_size;
		dsp_protocol->mmap_buffer_size =
		    audio_init_status.mmap_buffer_size;
	} else {
		/* This pcm task node is busy. Try to use another one. */
		ret = -EBUSY;
		goto unlock;
	}
	dsp_protocol->mmap_buffer = (short int *)
	    mmap((void *)0, dsp_protocol->mmap_buffer_size,
		 PROT_READ | PROT_WRITE, MAP_SHARED, dsp_protocol->fd, 0);

	if (dsp_protocol->mmap_buffer == NULL) {
		report_dsp_protocol("Cannot mmap data buffer", dsp_protocol);
		ret = -ENOMEM;
		goto unlock;
	}
	dsp_protocol->state = STATE_INITIALISED;
	report_dsp_protocol("connection stablished:", dsp_protocol);

	ret = 0;
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol DSP protocol reference pointer.
 * @param audio_params_data audio params to be sent to the dsp.
 * 
 * Send audio params to pcm task node and checks if
 * it was sent properly.
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_send_audio_params(dsp_protocol_t * dsp_protocol,
				   audio_params_data_t * audio_params_data)
{
	int ret;
	dsp_cmd_status_t audio_cmd_status;
	DENTER();
	if (dsp_protocol->state != STATE_INITIALISED) {
		report_dsp_protocol
		    ("Trying to send audio parameters from a non-valid state",
		     dsp_protocol);
		ret = -EIO;
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	audio_params_data->ds_stream_id = dsp_protocol->stream_id;
	if (write(dsp_protocol->fd, audio_params_data,
		  sizeof(audio_params_data_t)) < 0) {
		ret = -1;
		report_dsp_protocol("Could not send audio_params_data",
				    dsp_protocol);
		goto unlock;
	}
	if (read(dsp_protocol->fd, &audio_cmd_status, 
		sizeof(dsp_cmd_status_t)) < 0) {
		ret = -1;
		report_dsp_protocol("Could not receive DSP_CMD_STATUS",
				    dsp_protocol);
		goto unlock;
	}
	if (audio_cmd_status.status != DSP_OK) {
		ret = -1;
		report_dsp_protocol("DSP returned a diferent Status of DSP_OK",
				    dsp_protocol);
		report_return_value("DSP returned", audio_cmd_status.status);
		report_audio_params("Audio params sent", (*audio_params_data));
		goto unlock;
	}
	report_audio_params("Audio params sent", (*audio_params_data));
	ret = 0;
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol DSP protocol reference pointer.
 * @param speech_params_data audio params to be sent to the dsp.
 * 
 * Send audio params to pcm_rec task node and checks if
 * it was sent properly.
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_send_speech_params(dsp_protocol_t * dsp_protocol,
				    speech_params_data_t * speech_params_data)
{
	int ret;
	dsp_cmd_status_t audio_cmd_status;
	DENTER();
	if (dsp_protocol->state != STATE_INITIALISED) {
		report_dsp_protocol
		    ("Trying to send speech parameters from a non-valid state",
		     dsp_protocol);
		ret = -EIO;
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	speech_params_data->ds_stream_id = dsp_protocol->stream_id;
	if (write(dsp_protocol->fd, speech_params_data,
		  sizeof(speech_params_data_t)) < 0) {
		ret = -1;
		report_dsp_protocol("Could not send speech_params_data",
				    dsp_protocol);
		goto unlock;
	}
	if (read(dsp_protocol->fd, &audio_cmd_status, 
		sizeof(dsp_cmd_status_t)) < 0) {
		ret = -1;
		report_dsp_protocol("Could not receive DSP_CMD_STATUS",
				    dsp_protocol);
		goto unlock;
	}
	if (audio_cmd_status.status != DSP_OK) {
		ret = -1;
		report_dsp_protocol("DSP returned a diferent Status of DSP_OK",
				    dsp_protocol);
		report_return_value("DSP returned", audio_cmd_status.status);
		report_speech_params("Speech params sent",
				     (*speech_params_data));
		goto unlock;
	}
	report_speech_params("Speech params sent", (*speech_params_data));
	ret = 0;
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/* Execution phase features definitions */
/**
 * @param dsp_protocol dsp_protocol_t structure.
 *
 * It starts a playback section sending a DSP_CMD_PLAY. It flushes
 * all pending message comming from DSP side after DSP_CMD_PLAY.
 *
 * @return zero if success, otherwise a negative error code
 *          (-EIO - sending play from a non-valid state).
 */
int dsp_protocol_send_play(dsp_protocol_t * dsp_protocol)
{
	int ret;

	DENTER();
	if (dsp_protocol->state == STATE_UNINITIALISED) {
		report_dsp_protocol
		    ("Trying to send play from a non-valid state",
		     dsp_protocol);
		ret = -EIO;
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	if (dsp_protocol->state == STATE_PLAYING)
		ret = 0;
	else {
		if ((ret =
		     dsp_protocol_send_command(dsp_protocol,
					       DSP_CMD_PLAY)) == 0)
			dsp_protocol->state = STATE_PLAYING;
		dsp_protocol_flush(dsp_protocol);	
		//Both, read and write remain data on the mbx system
	}
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param data audio data buffer.
 * @param count amount of data to be copied (in TUint16).
 *
 * It copies audio data to the mmap area in the right moment.
 *
 * @return if success, it returns the amount of data was sent.
 * If called in a wrong moment, it returns 0.  Otherwise, a negative error code.
 */
int dsp_protocol_send_audio_data(dsp_protocol_t * dsp_protocol,
				 void *data,
				 unsigned short int count /* TUint16 */ )
{
	write_status_t write_status;
	data_write_t data_write;
	int ret = 0;
	DENTER();
	DPRINT("count %d\n", count);
	if (dsp_protocol->state != STATE_PLAYING) {
		report_dsp_protocol("Not in the STATE_PLAYING\n", dsp_protocol);
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;

	memcpy(dsp_protocol->mmap_buffer, data, count * 2);
	data_write.dsp_cmd = DSP_CMD_DATA_WRITE;
	data_write.data_size = count;
	if ((ret = write(dsp_protocol->fd, &data_write,
			 sizeof(data_write_t))) < 0)
		goto unlock;

	if ((ret = read(dsp_protocol->fd, &write_status,
			sizeof(write_status_t))) < 0)
		goto unlock;
	if (write_status.dsp_cmd == DSP_CMD_DATA_WRITE) {

		if (write_status.status == DSP_OK) {
			ret = count;
			DPRINT("%d words sent\n", ret);
		} else {
			DPRINT("Received a response different of DSP_OK\n");
			report_return_value("Returned value:",
					    write_status.status);
			report_dsp_protocol("Current dsp_protocol",
					    dsp_protocol);
			ret = 0;
		}
	} else {
		report_dsp_protocol("Could not send audio data", dsp_protocol);
		report_command("Returned cmd", write_status.dsp_cmd);
		ret = 0;
	}

      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;

}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param data audio data buffer.
 * @param count amount of data to be copied (in TUint16).
 *
 * It copies the audio data from the mmap area in the right moment.
 *
 * @return if success, it returns the amount of data was received.
 * If called in a wrong moment, it returns 0.  Otherwise, a negative error code.
 */
int dsp_protocol_receive_audio_data(dsp_protocol_t * dsp_protocol,
				    void *data, int count /* TU16int */ )
{
	read_status_t read_status;
	dsp_cmd_status_t audio_cmd_status;

	int ret = 0;
	DENTER();
	DPRINT("count %d\n", count);
	if (dsp_protocol->state != STATE_PLAYING) {
		report_dsp_protocol("Not in the STATE_PLAYING\n", dsp_protocol);
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	memcpy(data, dsp_protocol->mmap_buffer, count * 2);
	audio_cmd_status.dsp_cmd = DSP_CMD_DATA_READ;
	audio_cmd_status.status = DSP_OK;
	if ((ret = write(dsp_protocol->fd, &audio_cmd_status,
			 sizeof(dsp_cmd_status_t))) < 0)
		goto unlock;
	if ((ret = read(dsp_protocol->fd, &read_status,
			sizeof(read_status_t))) < 0)
		goto unlock;
	if (read_status.dsp_cmd == DSP_CMD_DATA_READ) {
		if (read_status.status == DSP_OK) {
			DPRINT("---------> DSP: ### %d ###\n",
			       read_status.frame_size);
			ret = count;
			DPRINT("%d words sent\n", ret);
		} else {
			report_dsp_protocol("Receive a status "
					"different from DSP_OK (skiping block)",
			     dsp_protocol);
			report_return_value("Returned: ", read_status.status);
			ret = 0;
		}

	} else {
		report_dsp_protocol("Could not receive audio data",
				    dsp_protocol);
		DPRINT("Returned cmd %d expected %d\n", read_status.dsp_cmd,
		       DSP_CMD_DATA_READ);
		report_return_value("Returned: ", read_status.status);
		ret = 0;
	}
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 *
 * It pauses a playback section sending a DSP_CMD_PAUSE.
 *
 * @return zero if success, otherwise a negative error code
 *          (-EIO - sending play from a non-valid state).
 */
int dsp_protocol_send_pause(dsp_protocol_t * dsp_protocol)
{
	int ret;
	DENTER();
	if (dsp_protocol->state != STATE_PLAYING) {
		report_dsp_protocol("Not in the STATE_PLAYING\n", dsp_protocol);
		ret = -EIO;
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	if (dsp_protocol->state == STATE_PAUSED)
		ret = 0;
	else {
		if ((ret =
		     dsp_protocol_send_command(dsp_protocol,
					       DSP_CMD_PAUSE)) == 0)
			dsp_protocol->state = STATE_PAUSED;
	}
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 *
 * It stops a playback section sending a DSP_CMD_STOP.
 *
 * @return zero if success, otherwise a negative error code
 *          (-EIO - sending play from a non-valid state).
 */
int dsp_protocol_send_stop(dsp_protocol_t * dsp_protocol)
{
	int ret;
	DENTER();
	if (dsp_protocol->state != STATE_PLAYING) {
		report_dsp_protocol("Not in the STATE_PLAYING\n", dsp_protocol);
		ret = -EIO;
		goto out;
	}
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	if (dsp_protocol->state == STATE_STOPPED)
		ret = 0;
	else {
		if ((ret =
		     dsp_protocol_send_command(dsp_protocol,
					       DSP_CMD_STOP)) == 0)
			dsp_protocol->state = STATE_STOPPED;
	}
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/* Deletion phase features definitions */
/**
 * @param dsp_protocol DSP protocol reference pointer to be uninitialized.
 * 
 * It closes the connection with pcm dsp task node. It flushes all
 * pending data before that. Closes the mmap area. Initialize the
 * dsp_protocol structure with unused values.
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_close_node(dsp_protocol_t * dsp_protocol)
{
	int ret;
	DENTER();
	if (dsp_protocol->state != STATE_UNINITIALISED) {
		if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
			goto out;
		if ((ret = dsp_protocol_flush(dsp_protocol)) < 0)
			goto unlock;
		if ((ret = dsp_protocol_send_command(dsp_protocol,
						     DSP_CMD_CLOSE)) < 0)
			goto unlock;
	}

	if (dsp_protocol->mmap_buffer)
		munmap(dsp_protocol->mmap_buffer,
		       dsp_protocol->mmap_buffer_size);
	close(dsp_protocol->fd);
	dsp_protocol->fd = -1;
	free(dsp_protocol->device);
	dsp_protocol->device = NULL;
	dsp_protocol->state = STATE_UNINITIALISED;
	dsp_protocol->mute = 0;
	dsp_protocol->stream_id = 0;
	dsp_protocol->bridge_buffer_size = 0;
	dsp_protocol->mmap_buffer_size = 0;
	dsp_protocol->mmap_buffer = NULL;
	ret = 0;
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol DSP protocol reference pointer.
 * 
 * It frees all the allocated memory. It sets NULL to the pointer. 
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_destroy(dsp_protocol_t ** dsp_protocol)
{
	int ret = 0;
	DENTER();
#ifdef USE_RESOURCE_MANAGER
        if ((*dsp_protocol)->dbus_connection)
                dbus_connection_unref((*dsp_protocol)->dbus_connection);
#endif /* USE_RESOURCE_MANAGER */
	if (*dsp_protocol) {
		if ((*dsp_protocol)->device)
			free((*dsp_protocol)->device);
		free((*dsp_protocol));
		*dsp_protocol = NULL;
	}
	DLEAVE(ret);
	return ret;
}

/* controls features definitions */
/**
 * dsp_protocol_set_volume:
 * 
 * @param dsp_protocol dsp_protocol_t structure.
 * @param left left channel volume value (0 - 100).
 * @param right right channel volume value (0 - 100).
 * 
 * It changes volume data for both left and right channels.
 * It receives both values in a 0 - 100 scale. 
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_set_volume(dsp_protocol_t * dsp_protocol,
			    unsigned char left, unsigned char right)
{
	int ret;
	dsp_cmd_status_t audio_cmd_status;
	volume_data_t volume_data;
	panning_data_t panning_data;

	DENTER();
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	dsp_protocol_linear2Q15(left > right ? left : right, &volume_data.scale,
				&volume_data.power2);
	volume_data.dsp_cmd = DSP_CMD_SET_VOLUME;
	if ((ret =
	     write(dsp_protocol->fd, &volume_data, sizeof(volume_data_t))) < 0)
		goto unlock;
	if (read(dsp_protocol->fd, &audio_cmd_status, 
		sizeof(dsp_cmd_status_t)) < 0) {
		ret = -EINVAL;
		report_dsp_protocol("Could not receive DSP_CMD_STATUS",
				    dsp_protocol);
		goto unlock;
	}
	report_return_value("Received", audio_cmd_status.status);
	ret = 0;
	if (audio_cmd_status.status != DSP_OK)
		ret = -EIO;
	if (ret == 0) {		/*if sucess till here, update panning info */
		panning_data.dsp_cmd = DSP_CMD_SET_PANNING;
		panning_data.steps = PANNING_STEP;
		if (left != right) {
			panning_data.left_gain =
			    left >
			    right ? 0x4000 : (1.0 * left) / right * 0x4000;
			panning_data.right_gain =
			    right >
			    left ? 0x4000 : (1.0 * right) / left * 0x4000;
		} else {
			panning_data.left_gain = 0x4000;
			panning_data.right_gain = 0x4000;
		}
		DPRINT("left gain %x right gain %x\n", panning_data.left_gain,
		       panning_data.right_gain);
		if ((ret =
		     write(dsp_protocol->fd, &panning_data,
			   sizeof(panning_data_t))) < 0)
			goto unlock;
		if (read(dsp_protocol->fd, &audio_cmd_status,
		         sizeof(dsp_cmd_status_t)) < 0) {
			ret = -EINVAL;
			report_dsp_protocol("Could not receive DSP_CMD_STATUS",
					    dsp_protocol);
			goto unlock;
		}
		ret = 0;
		if (audio_cmd_status.status != DSP_OK)
			ret = -EIO;

	}
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param left left channel volume value output (0 - 100).
 * @param right right channel volume value output (0 - 100).
 * 
 * It returns volume data for both left and right channels.
 * It provides both values in a 0 - 100 scale. 
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_get_volume(dsp_protocol_t * dsp_protocol,
			    unsigned char *left, unsigned char *right)
{
	unsigned short int tmp;
	int ret;
	audio_status_info_t audio_status_info;
	DENTER();
	tmp = DSP_CMD_STATE;
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;

	if (write(dsp_protocol->fd, &tmp, sizeof(short int)) < 0) {
		ret = -EIO;
		goto unlock;
	}
	if ((ret = read(dsp_protocol->fd, &audio_status_info,
			sizeof(audio_status_info_t))) < 0) {
		report_dsp_protocol("Could not read audio_status_info",
				    dsp_protocol);
		goto unlock;
	}
	dsp_protocol->state = audio_status_info.status;
	report_audio_status_info("Received:", audio_status_info);
	dsp_protocol_Q152linear(audio_status_info.vol_scale,
				audio_status_info.vol_power2, &tmp);
	*left = tmp;
	*right = tmp;
	if (audio_status_info.number_channels == CHANNELS_2) {
		if (audio_status_info.left_gain > audio_status_info.right_gain){
			float result =
			    *right * audio_status_info.right_gain / (0x4000 *
								     1.0);
			*right = result;
			if ((result - *right) > 0.5)
				(*right)++;
		}
		if (audio_status_info.left_gain < audio_status_info.right_gain){
			float result =
			    *left * audio_status_info.left_gain / (0x4000 *
								   1.0);
			*left = result;
			if ((result - *left) > 0.5)
				(*left)++;
		}
	}
	ret = 0;
      unlock:
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param mute mute value (0 unmuted - 1 muted).
 * 
 * It changes mute state of DSP task node sending DSP_CMD_[UN]MUTE.
 * Provide 0 in mute to unmute, and 1 in mute to mute.
 * 
 * @return zero if success, otherwise a negative error code.
 */
int dsp_protocol_set_mute(dsp_protocol_t * dsp_protocol, unsigned char mute)
{
	int ret = 0;
	int command;
	DENTER();
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	if (mute == 1)
		command = DSP_CMD_MUTE;
	else
		command = DSP_CMD_UNMUTE;
	ret = dsp_protocol_send_command(dsp_protocol, command);
	dsp_protocol->mute = mute;
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * 
 * It returns mute state of DSP task node.
 * 
 * @return zero if success, it returns current state of mute.
 * 0 means not muted. 1 means muted. otherwise a negative error code.
 */
int dsp_protocol_get_mute(dsp_protocol_t * dsp_protocol)
{
	int ret = 0;
	DENTER();
	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	ret = dsp_protocol_update_state(dsp_protocol);
	if (ret >= 0)
		ret = dsp_protocol->mute;
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;
}

/*miscelaneos features definitions */
/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param enable enabled state of mic. 0 disable. 1 enabled. 
 * 
 * It modifies enabled state of mic.
 * 
 * @return zero if success, otherwise a negative error code.
 */
#ifdef USE_RESOURCE_MANAGER
int dsp_protocol_set_mic_enabled(dsp_protocol_t * dsp_protocol, int enabled)
{
	int ret = 0;
	DBusMessage *message;
	DBusMessage *reply = NULL;
	DENTER();
	if (dsp_protocol->dbus_connection &&
	    (message = dbus_message_new_method_call(AUDIO_PM_SERVICE,
				AUDIO_PM_RECORD_RESOURCE, RESOURCE_INTERFACE,
				enabled ? "request" : "release"))) {
		if (!enabled) {
			dbus_int32_t handle = 0;
			/* assuming handle is always 0 for this resource */
			dbus_message_append_args(message,
						 DBUS_TYPE_INT32, &handle,
						 DBUS_TYPE_INVALID);
		}
		reply = dbus_connection_send_with_reply_and_block(
					dsp_protocol->dbus_connection,
					message, RESOURCE_TIMEOUT, NULL);
                /* TODO: check return value(s), append args for request()
                 * These parts of the RM spec may still change so not done yet
                 */
		dbus_message_unref(message);
	}
	if (reply) {
		dbus_message_unref(reply);
	} else {
		DPRINT("Error acquiring PM resource for recording\n");
		#ifdef ERROR_ON_PM_FAILURE
		ret = -EIO;
		#endif
	}
	DLEAVE(ret);
	return ret;
}

#else /* !USE_RESOURCE_MANAGER */
int dsp_protocol_set_mic_enabled(dsp_protocol_t * dsp_protocol, int enabled)
{
	int mic_fd;
	int ret;
	char mic_control_name[] = "/sys/devices/platform/audio-i2c/mic_enable";
	DENTER();
	if ((mic_fd = open(mic_control_name, O_WRONLY)) < 0) {
		DPRINT("Error opening: %s\n", mic_control_name);
#ifdef ERROR_ON_PM_FAILURE
		ret = -EIO;
#else
		ret = 0;
#endif
		goto out;
	}

	write(mic_fd, (enabled ? "1" : "0"), sizeof(char));
	close(mic_fd);
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}
#endif /* USE_RESOURCE_MANAGER */

/**
 * @param dsp_protocol dsp_protocol_t structure.
 * @param device dsp task node file name.
 * 
 * It opens a dsp task node and queries for its states.
 * 
 * @return if success, it returns number of channels whose dsp task is 
 * currently using (1 - mono. 2 stero). otherwise a negative error code.
 */
int dsp_protocol_probe_node(dsp_protocol_t * dsp_protocol, const char *device)
{
	int ret;
	DENTER();
	if (dsp_protocol->state != STATE_UNINITIALISED) {
		report_dsp_protocol
		    ("Trying to send open node from a non-valid state",
		     dsp_protocol);
		ret = -EIO;
		goto out;
	}

	dsp_protocol->fd = open(device, O_RDWR);
	if (dsp_protocol->fd < 0) {
		DERROR("Could not open pcm device file %s\n", device);
		ret = errno;
		goto out;
	}
	dsp_protocol->device = strdup(device);
	dsp_protocol_get_sem(dsp_protocol);

	if ((ret = dsp_protocol_lock_dev(dsp_protocol)) < 0)
		goto out;
	dsp_protocol->device = strdup(device);
	ret = dsp_protocol_update_state(dsp_protocol);
	if (ret != CHANNELS_1 && ret != CHANNELS_2)
		ret = CHANNELS_1;
	dsp_protocol_unlock_dev(dsp_protocol);
      out:
	DLEAVE(ret);
	return ret;

}

/**
 * @param str string with a number.
 * @param val holder for the conversion result.
 * 
 * It converts a string to a number (long).
 * 
 * @return zero if success, otherwise a negative error code.
 */
int safe_strtol(const char *str, long *val)
{
	char *end;
	long v;
	int ret;
	DENTER();
	if (!*str) {
		ret = -EINVAL;
		goto out;
	}
	errno = 0;
	v = strtol(str, &end, 0);
	if (errno) {
		ret = -errno;
		goto out;
	}
	if (*end) {
		ret = -EINVAL;
		goto out;
	}
	*val = v;
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}
/* internal features definitions */
/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * 
 * Tries to read all the pending data.
 *
 * @return zero. success
 */
static int dsp_protocol_flush(dsp_protocol_t * dsp_protocol)
{
	struct pollfd pollf;
	int ret = 0;
	int tmp;
	DENTER();
	pollf.fd = dsp_protocol->fd;
	pollf.events = POLLIN;
	while (poll(&pollf, 1, 0) > 0) {
		if (read(dsp_protocol->fd, &tmp, sizeof(short int)) == 0)
			/* Test end of file */
			break;
#ifdef DEBUG
		fprintf(DEBUG_OUTPUT, ".");
#endif
	}
#ifdef DEBUG
	fprintf(DEBUG_OUTPUT, "\n");
#endif
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * @param command command value to be sent to the pcm task node.
 * 
 * Send the command to pcm task node and checks if
 * it was sent properly.
 * 
 * @return zero if success, otherwise a negative error code.
 */
static int dsp_protocol_send_command(dsp_protocol_t * dsp_protocol,
				     const short int command)
{
	dsp_cmd_status_t audio_cmd_status;
	int ret = 0;
	short int tmp;
	DENTER();
	report_command("Sending", command);
	tmp = command;
	if (write(dsp_protocol->fd, &tmp, sizeof(short int)) < 0) {
		report_dsp_protocol("Could not send", dsp_protocol);
		ret = -EIO;
	}

	if (read(dsp_protocol->fd, &audio_cmd_status, 
		sizeof(dsp_cmd_status_t)) < 0) {
		ret = -1;
		report_dsp_protocol("Could not receive DSP_CMD_STATUS",
				    dsp_protocol);
		goto out;
	}
/*	report_command("Received", audio_cmd_status.dsp_cmd);
	report_return_value("Received",  audio_cmd_status.status);*/
	DPRINT("audio_cmd_status.dsp_cmd: 0x%x\n", audio_cmd_status.dsp_cmd);
	DPRINT("audio_cmd_status.status: 0x%x\n", audio_cmd_status.status);
	if (audio_cmd_status.status != DSP_OK)
		ret = -EIO;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param input 0 - 100 value to be converted to Q15. 
 * @param scale scale value of Q15 format.
 * @param power2 power of 2 value of Q15 format.
 * 
 * Converts a 0 - 100 value to a Q15 format value.
 * 
 */
static void dsp_protocol_linear2Q15(const unsigned short int input,
				    unsigned short int *scale, 
				    unsigned short int *power2)
{
	unsigned long int val = MAGIC_NUMBER * input;
	DENTER();
	if (input == 0) {
		*scale = 0;
		*power2 = 0;
	} else {
		*power2 = 1;
		while (val < 0x40000000) {
			(*power2)--;
			val <<= 1;
		}
		*scale = val >> 16;
	}
	DPRINT("Resulted scale %d and power2 %d from input %d\n", *scale,
	       *power2, input);
	DLEAVE(0);
}

/**
 * @param scale scale value of Q15 format.
 * @param power2 power of 2 value of Q15 format.
 * @param output 0 - 100 resulted value.
 * 
 * Converts a Q15 format value to a 0 - 100 value.
 * 
 */
static void dsp_protocol_Q152linear(const unsigned short int scale,
				    const unsigned short int power2, unsigned short int *output)
{
	float result = scale * 1.0 / 0x8000 * (1 << power2) * 100.0;
	DENTER();
	*output = (short int)(result);
	if ((result - *output) > 0.5)
		(*output)++;
	DPRINT("Resulted linear: %d from scale %d and power2 %d\n", *output,
	       scale, power2);
	DLEAVE(0);
}

/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * 
 * Update dsp_protocol_t structure info. Queries dsp task
 * for a audio_status_info structure.
 * 
 * @return if success, it returns number of channels whose dsp task is 
 * currently using. otherwise a negative error code.
 */
static int dsp_protocol_update_state(dsp_protocol_t * dsp_protocol)
{
	int ret;
	short int tmp;
	audio_status_info_t audio_status_info;
	DENTER();

	if ((ret = dsp_protocol_flush(dsp_protocol)) < 0)
		goto out;
	tmp = DSP_CMD_STATE;
	if (write(dsp_protocol->fd, &tmp, sizeof(short int)) < 0) {
		ret = -EIO;
		goto out;
	}
	if ((ret = read(dsp_protocol->fd, &audio_status_info,
			sizeof(audio_status_info_t))) < 0) {
		report_dsp_protocol("Could not read audio_status_info",
				    dsp_protocol);
		goto out;
	}
	report_audio_status_info("Received:", audio_status_info);
	dsp_protocol->stream_id = audio_status_info.stream_id;
	dsp_protocol->bridge_buffer_size = audio_status_info.bridge_buffer_size;
	dsp_protocol->mmap_buffer_size = audio_status_info.mmap_buffer_size;
	dsp_protocol->state = audio_status_info.status;
#ifndef NORMAL_DSP_TASK
	dsp_protocol->mute = audio_status_info.mute;
#endif
	report_dsp_protocol("connection stablished:", dsp_protocol);
	ret = audio_status_info.number_channels;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * 
 * It produces the semaphore ID and connects to it.
 * 
 * @return zero if success, otherwise a negative error code.
 */
static inline int dsp_protocol_get_sem(dsp_protocol_t * dsp_protocol)
{
	union semun sem_val;	/* semaphore value, for semctl(). */

	int rc;
	int ret;
	/* identifier returned by ftok() */
	key_t set_key;
	DENTER();
	/* generate a "unique" key for our set, using the */
	/* directory "/dev/dsptask/xxxx".      */
	set_key = ftok(dsp_protocol->device, 0);
	if (set_key == -1) {
		DPRINT("ftok: %d\n", errno);
		ret = -ENODEV;
		goto out;
	}
	DPRINT("key %d\n", set_key);

	/* now we can use 'set_key' to generate a set id, for example. */
	dsp_protocol->sem_set_id = semget(set_key, 1, 0666);
	if (dsp_protocol->sem_set_id == -1) {
		DPRINT("semget %d\n", errno);
		dsp_protocol->sem_set_id = semget(set_key, 1, IPC_CREAT | 0666);
		if (dsp_protocol->sem_set_id == -1) {
			DPRINT("semget: IPC_CREAT: %d\n", errno);
			ret = -ENODEV;
			goto out;
		}
		DPRINT("Initialising the semaphore\n");
		sem_val.val = 1;
		rc = semctl(dsp_protocol->sem_set_id, 0, SETVAL, sem_val);
		if (rc == -1) {
			DPRINT("semctl %d\n", errno);
			ret = -ENODEV;
			goto out;
		}
	}
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * 
 * It waits until be able to hold the semaphore to access the dsp task node.
 * If the current process already has the semaphore, returns success.
 * 
 * @return zero if success, otherwise a negative error code.
 */
static inline int dsp_protocol_lock_dev(dsp_protocol_t * dsp_protocol)
{
	int ret;
	struct sembuf sem_op;
	DENTER();
	ret = pthread_mutex_trylock(&(dsp_protocol->mutex));
	if (ret != 0) {
		DPRINT("No lock %d\n", ret);
		if (errno == EBUSY)
			ret = 0;
		goto out;
	}
	sem_op.sem_num = 0;
	sem_op.sem_op = -1;
	sem_op.sem_flg = 0;
	DPRINT("requesting semaphore (dev)\n");
	if (semop(dsp_protocol->sem_set_id, &sem_op, 1) == -1) {
		DPRINT("semop %d\n", errno);
		pthread_mutex_unlock(&(dsp_protocol->mutex));
		ret = -errno;
	}
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * @param dsp_protocol  dsp_protocol_t structure.
 * 
 * It releases the semaphore.
 * 
 * @return zero if success, otherwise a negative error code.
 */
static inline int dsp_protocol_unlock_dev(dsp_protocol_t * dsp_protocol)
{
	struct sembuf sem_op;
	DENTER();
	sem_op.sem_num = 0;
	sem_op.sem_op = 1;
	sem_op.sem_flg = 0;
	DPRINT("Releasing\n");
	semop(dsp_protocol->sem_set_id, &sem_op, 1);

	pthread_mutex_unlock(&(dsp_protocol->mutex));
	DLEAVE(0);
	return 0;
}


