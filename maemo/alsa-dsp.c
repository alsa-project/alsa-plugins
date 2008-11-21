/**
 * @file alsa-dsp.c
 * @brief Alsa External plugin: I/O plugin
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
#include <alsa/pcm_external.h>
#include "list.h"
#include "debug.h"
#include "dsp-protocol.h"
#include "constants.h"

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))
/** 
 * Device node file name list.
 */
typedef struct {
	char *device;
	struct list_head list;
} device_list_t;

/** 
 * Holds the need information: list of playback and recording devices,
 * current format, sample_rate, bytes per frame and pointer to ring
 * buffer.
 */
typedef struct snd_pcm_alsa_dsp {
	snd_pcm_ioplug_t io;
	dsp_protocol_t *dsp_protocol;
	int format;
	int sample_rate;
	int bytes_per_frame;
	snd_pcm_sframes_t hw_pointer;
	device_list_t playback_devices;
	device_list_t recording_devices;
} snd_pcm_alsa_dsp_t;

static snd_pcm_alsa_dsp_t *free_ref;
/**
 * @param io pcm io plugin configured to Alsa libs.
 *
 * It starts the playback sending a DSP_CMD_PLAY.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_start(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret;
	DENTER();
	DPRINT("IO_STREAM %d == SND_PCM_STREAM_PLAYBACK %d\n", io->stream,
	       io->stream == SND_PCM_STREAM_PLAYBACK);
	if (io->stream != SND_PCM_STREAM_PLAYBACK)
		dsp_protocol_set_mic_enabled(alsa_dsp->dsp_protocol, 1);
	ret = dsp_protocol_send_play(alsa_dsp->dsp_protocol);
	DLEAVE(ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * It starts the playback sending a DSP_CMD_STOP.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_stop(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret;
	DENTER();
	ret = dsp_protocol_send_stop(alsa_dsp->dsp_protocol);
	if (io->stream != SND_PCM_STREAM_PLAYBACK)
		dsp_protocol_set_mic_enabled(alsa_dsp->dsp_protocol, 0);

	DLEAVE(ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * It returns the position of current period consuming.
 *
 * @return on success, returns current position, otherwise a negative
 * error code.
 */
static snd_pcm_sframes_t alsa_dsp_pointer(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	snd_pcm_sframes_t ret;
	DENTER();
	ret = alsa_dsp->hw_pointer;
	if (alsa_dsp->hw_pointer == 0)
		alsa_dsp->hw_pointer =
		    io->period_size * alsa_dsp->bytes_per_frame;
	else
		alsa_dsp->hw_pointer = 0;
	DLEAVE((int)ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * It transfers the audio data to dsp side.
 *
 * @return on success, returns amount of data transfered,
 * otherwise a negative error code.
 */
static snd_pcm_sframes_t alsa_dsp_transfer(snd_pcm_ioplug_t * io,
					   const snd_pcm_channel_area_t * areas,
					   snd_pcm_uframes_t offset,
					   snd_pcm_uframes_t size)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	DENTER();
	char *buf;
	int words;
	ssize_t result;

	words = size * alsa_dsp->bytes_per_frame;
	words /= 2;
	DPRINT("***** Info: words %d size %lu bpf: %d\n", words, size,
	       alsa_dsp->bytes_per_frame);
	if (words > alsa_dsp->dsp_protocol->mmap_buffer_size) {
		DERROR("Requested too much data transfer (playing only %d)\n",
		       alsa_dsp->dsp_protocol->mmap_buffer_size);
		words = alsa_dsp->dsp_protocol->mmap_buffer_size;
	}
	if (alsa_dsp->dsp_protocol->state != STATE_PLAYING) {
		DPRINT("I did nothing - No start sent\n");
		alsa_dsp_start(io);
	}
	/* we handle only an interleaved buffer */
	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
	if (io->stream == SND_PCM_STREAM_PLAYBACK)
		result =
		    dsp_protocol_send_audio_data(alsa_dsp->dsp_protocol, buf,
						 words);
	else
		result =
		    dsp_protocol_receive_audio_data(alsa_dsp->dsp_protocol, buf,
						    words);
	result *= 2;
	result /= alsa_dsp->bytes_per_frame;
	alsa_dsp->hw_pointer += result;
	DLEAVE(result);
	return result;
}

/**
 * @param device_list a list of device names to be freed.
 *
 * It passes a list of device names and frees each node.
 *
 * @return zero (success).
 */
static int free_device_list(device_list_t * device_list)
{
	struct list_head *pos, *q;
	device_list_t *tmp;
	list_for_each_safe(pos, q, &device_list->list) {
		tmp = list_entry(pos, device_list_t, list);
		list_del(pos);
		free(tmp->device);
		free(tmp);
	}
	return 0;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * Closes the connection with the pcm dsp task. It
 * destroies all allocated data. 
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_close(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret = 0;
	DENTER();
	ret = dsp_protocol_close_node(alsa_dsp->dsp_protocol);
	dsp_protocol_destroy(&(alsa_dsp->dsp_protocol));
	free_device_list(&(alsa_dsp->playback_devices));
	free_device_list(&(alsa_dsp->recording_devices));
	DLEAVE(ret);
	return ret;
}

/**
 * @param map the values to be mapped
 * @param value the search key
 * @param steps how many keys should be checked 
 *
 * Maps a value to another. 
 *
 * @return on success, returns mapped value, otherwise a negative error code.
 */
static int map_value(int *map, int value, int steps)
{
	int i;
	for (i = 0; i < steps; i++)
		if (map[i * 2] == value)
			return map[i * 2 + 1];
	return -1;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 * @param params 
 *
 * It checks if the pcm format and rate are supported. 
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_hw_params(snd_pcm_ioplug_t * io,
			      snd_pcm_hw_params_t * params)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret = 0;
	int map_sample_rates[] = {
		8000, SAMPLE_RATE_8KHZ,
		11025, SAMPLE_RATE_11_025KHZ,
		12000, SAMPLE_RATE_12KHZ,
		16000, SAMPLE_RATE_16KHZ,
		22050, SAMPLE_RATE_22_05KHZ,
		24000, SAMPLE_RATE_24KHZ,
		32000, SAMPLE_RATE_32KHZ,
		44100, SAMPLE_RATE_44_1KHZ,
		48000, SAMPLE_RATE_48KHZ
	};
	int map_formats[] = {
		SND_PCM_FORMAT_A_LAW, DSP_AFMT_ALAW,
		SND_PCM_FORMAT_MU_LAW, DSP_AFMT_ULAW,
		SND_PCM_FORMAT_S16_LE, DSP_AFMT_S16_LE,
		SND_PCM_FORMAT_U8, DSP_AFMT_U8,
		SND_PCM_FORMAT_S8, DSP_AFMT_S8,
		SND_PCM_FORMAT_S16_BE, DSP_AFMT_S16_BE,
		SND_PCM_FORMAT_U16_LE, DSP_AFMT_U16_LE,
		SND_PCM_FORMAT_U16_BE, DSP_AFMT_U16_BE
	};
	DENTER();
	DPRINT("Checking Format- Ret %d\n", ret);
	alsa_dsp->format = map_value(map_formats, io->format,
				     io->stream ==
				     SND_PCM_STREAM_PLAYBACK ?
				     ARRAY_SIZE(map_formats) : 3);
	if (alsa_dsp->format < 0) {
		DERROR("*** ALSA-DSP: unsupported format %s\n",
		       snd_pcm_format_name(io->format));
		ret = -EINVAL;
	}
	DPRINT("Format is Ok. Checking rate. Ret %d\n", ret);

	alsa_dsp->sample_rate = map_value(map_sample_rates, io->rate,
					  io->stream ==
					  SND_PCM_STREAM_PLAYBACK ?
					  ARRAY_SIZE(map_sample_rates) : 1);
	if (alsa_dsp->sample_rate < 0) {
		ret = -EINVAL;
		DERROR("** ALSA - DSP - Unsuported Sample Rate! **\n");
	}
	DPRINT("Rate is ok. Calculating WPF. Ret %d\n", ret);

	alsa_dsp->bytes_per_frame =
	    ((snd_pcm_format_physical_width(io->format) * io->channels) / 8);
	DPRINT("WPF: %d width %d channels %d\n", alsa_dsp->bytes_per_frame,
	       snd_pcm_format_physical_width(io->format), io->channels);

	DLEAVE(ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 * 
 * It sends the audio parameters to pcm task node (formats, channels, 
 * access, rates). It is assumed that everything is proper set.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_prepare(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	audio_params_data_t params;
	speech_params_data_t sparams;
	int ret = 0;
	char *tmp;
	DENTER();

	alsa_dsp->hw_pointer = 0;
	if (alsa_dsp->dsp_protocol->state != STATE_INITIALISED) {
		tmp = strdup(alsa_dsp->dsp_protocol->device);
		ret = dsp_protocol_close_node(alsa_dsp->dsp_protocol);
		if (!ret)
			dsp_protocol_open_node(alsa_dsp->dsp_protocol, tmp);
		free(tmp);
	}
	if (ret == 0) {
		if (io->stream == SND_PCM_STREAM_PLAYBACK) {
			params.dsp_cmd = DSP_CMD_SET_PARAMS;
			params.dsp_audio_fmt = alsa_dsp->format;
			params.sample_rate = alsa_dsp->sample_rate;
			params.number_channels = io->channels;
			params.ds_stream_id = 0;
			params.stream_priority = 0;
			if (dsp_protocol_send_audio_params
			    (alsa_dsp->dsp_protocol, &params) < 0) {
				ret = -EIO;
				DERROR("Error in send params data\n");
			} else
				DPRINT("Sending params data is ok\n");
		} else {
			sparams.dsp_cmd = DSP_CMD_SET_SPEECH_PARAMS;
			sparams.audio_fmt = alsa_dsp->format;
			sparams.sample_rate = alsa_dsp->sample_rate;
			sparams.ds_stream_id = 0;
			sparams.stream_priority = 0;
			sparams.frame_size = io->period_size;
			DPRINT("frame size %u\n", sparams.frame_size);
			if (dsp_protocol_send_speech_params
			    (alsa_dsp->dsp_protocol, &sparams) < 0) {
				ret = -EIO;
				DERROR("Error in send speech params data\n");
			} else
				DPRINT("Sending speech params data is ok\n");

		}
	}
	DLEAVE(ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * It pauses the playback sending a DSP_CMD_PAUSE.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_pause(snd_pcm_ioplug_t * io, int enable)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret;
	DENTER();
	ret = dsp_protocol_send_pause(alsa_dsp->dsp_protocol);
	DLEAVE(ret);
	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 *
 * It starts the playback sending a DSP_CMD_PLAY.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_resume(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_dsp_t *alsa_dsp = io->private_data;
	int ret;
	DENTER();
	ret = dsp_protocol_send_play(alsa_dsp->dsp_protocol);
	DLEAVE(ret);
	return ret;
}

/**
 * @param alsa_dsp the structure to be configured.
 * 
 * It configures constraints about formats, channels, access, rates, 
 * periods and buffer size. It exports the supported constraints by the
 * dsp task node to the alsa plugin library.
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_configure_constraints(snd_pcm_alsa_dsp_t * alsa_dsp)
{
	snd_pcm_ioplug_t *io = &alsa_dsp->io;
	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED
	};
	static const unsigned int formats[] = {
		SND_PCM_FORMAT_U8,	/* DSP_AFMT_U8 */
		SND_PCM_FORMAT_S16_LE,	/* DSP_AFMT_S16_LE */
		SND_PCM_FORMAT_S16_BE,	/* DSP_AFMT_S16_BE */
		SND_PCM_FORMAT_S8,	/* DSP_AFMT_S8 */
		SND_PCM_FORMAT_U16_LE,	/* DSP_AFMT_U16_LE */
		SND_PCM_FORMAT_U16_BE,	/* DSP_AFMT_U16_BE */
		SND_PCM_FORMAT_A_LAW,	/* DSP_AFMT_ALAW */
		SND_PCM_FORMAT_MU_LAW	/* DSP_AFMT_ULAW */
	};
	static const unsigned int formats_recor[] = {
		SND_PCM_FORMAT_S16_LE,	/* DSP_AFMT_S16_LE */
		SND_PCM_FORMAT_A_LAW,	/* DSP_AFMT_ALAW */
		SND_PCM_FORMAT_MU_LAW	/* DSP_AFMT_ULAW */
	};
	static const unsigned int bytes_list[] = {
		1U << 11, 1U << 12
	};
	static const unsigned int bytes_list_rec_8bit[] = {
		/* It must be multiple of 80... less than or equal to 800 */
		80, 160, 240, 320, 400, 480, 560, 640, 720, 800
	};

	int ret, err;
	DENTER();
	/* Configuring access */
	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
						 ARRAY_SIZE(access_list),
						 access_list)) < 0) {
		ret = err;
		goto out;
	}
	if (io->stream == SND_PCM_STREAM_PLAYBACK) {
		/* Configuring formats */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
						   ARRAY_SIZE(formats),
						   formats)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring channels */
		if ((err = 
		     snd_pcm_ioplug_set_param_minmax(io,
						     SND_PCM_IOPLUG_HW_CHANNELS,
						     1, 2)) < 0) {
			ret = err;
			goto out;
		}

		/* Configuring rates */
		if ((err =
		     snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
						     8000, 48000)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring periods */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io,
						 SND_PCM_IOPLUG_HW_PERIOD_BYTES,
						 ARRAY_SIZE(bytes_list),
						 bytes_list)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring buffer size */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io,
						 SND_PCM_IOPLUG_HW_BUFFER_BYTES,
						 ARRAY_SIZE(bytes_list),
						 bytes_list)) < 0) {
			ret = err;
			goto out;
		}

	} else {
		/* Configuring formats */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, 
						   SND_PCM_IOPLUG_HW_FORMAT,
						   ARRAY_SIZE(formats_recor),
						   formats_recor)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring channels */
		if ((err = snd_pcm_ioplug_set_param_minmax(io,
						    SND_PCM_IOPLUG_HW_CHANNELS,
						    1, 1)) < 0) {
			ret = err;
			goto out;
		}

		/* Configuring rates */
		if ((err =
		     snd_pcm_ioplug_set_param_minmax(io, 
			                             SND_PCM_IOPLUG_HW_RATE,
						     8000, 8000)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring periods */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io, 
			                        SND_PCM_IOPLUG_HW_PERIOD_BYTES,
						ARRAY_SIZE
						(bytes_list_rec_8bit),
						bytes_list_rec_8bit)) < 0) {
			ret = err;
			goto out;
		}
		/* Configuring buffer size */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, 
			                        SND_PCM_IOPLUG_HW_BUFFER_BYTES,
						   ARRAY_SIZE
						   (bytes_list_rec_8bit),
						   bytes_list_rec_8bit)) < 0) {
			ret = err;
			goto out;
		}

	}

	if ((err = snd_pcm_ioplug_set_param_minmax(io,
						   SND_PCM_IOPLUG_HW_PERIODS,
						   2, 1024)) < 0) {
		ret = err;
		goto out;
	}
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * Alsa-lib callback structure.
 */
static snd_pcm_ioplug_callback_t alsa_dsp_callback = {
	.start = alsa_dsp_start,
	.stop = alsa_dsp_stop,
	.pointer = alsa_dsp_pointer,
	.transfer = alsa_dsp_transfer,
	.close = alsa_dsp_close,
	.hw_params = alsa_dsp_hw_params,
	.prepare = alsa_dsp_prepare,
	.pause = alsa_dsp_pause,
	.resume = alsa_dsp_resume,
};

/**
 * @param alsa_dsp the structure to be configured.
 * 
 * It probes all configured dsp task devices to be available for 
 * this plugin. It will use first dsp task device whose is in
 * UNINITIALISED state. 
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_dsp_open_dsp_task(snd_pcm_alsa_dsp_t * alsa_dsp,
				  device_list_t * device_list)
{
	int err = -EINVAL;
	device_list_t *tmp;
	DENTER();
	DPRINT("Looking for a dsp device node \n");
	list_for_each_entry(tmp, &(device_list->list), list) {
		DPRINT("Trying to use %s\n", tmp->device);
		if ((err =
		     dsp_protocol_open_node(alsa_dsp->dsp_protocol,
					    tmp->device)) < 0) {
			DPRINT("%s is not available now\n", tmp->device);
			dsp_protocol_close_node(alsa_dsp->dsp_protocol);
		} else
			break;
	}
	if (err < 0) {
		DPRINT("No valid dsp task nodes for now. Exiting.\n");
	}
	DLEAVE(err);
	return err;
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
static int fill_string_list(snd_config_t * n, device_list_t * device_list)
{
	snd_config_iterator_t j, nextj;
	device_list_t *tmp;
	long idx = 0;
	int ret;
	DENTER();
	INIT_LIST_HEAD(&device_list->list);
	snd_config_for_each(j, nextj, n) {
		snd_config_t *s = snd_config_iterator_entry(j);
		const char *id_number;
		long k;
		if (snd_config_get_id(s, &id_number) < 0)
			continue;
		if (safe_strtol(id_number, &k) < 0) {
			SNDERR("id of field %s is not an integer", id_number);
			ret = -EINVAL;
			goto out;
		}
		if (k == idx) {
			idx++;
			/* add to available dsp task nodes */
			tmp = (device_list_t *) malloc(sizeof(device_list_t));
			if (snd_config_get_ascii(s, &(tmp->device)) < 0) {
				SNDERR("invalid ascii string for id %s\n",
				       id_number);
				ret = -EINVAL;
				goto out;
			}

			list_add(&(tmp->list), &(device_list->list));
		}

	}
	ret = 0;
      out:
	DLEAVE(ret);
	return ret;
}

/**
 * It initializes the alsa plugin. It reads the parameters and creates the 
 * connection with the pcm device file.
 *
 * @return  zero if success, otherwise a negative error code.
 */
SND_PCM_PLUGIN_DEFINE_FUNC(alsa_dsp)
{
	snd_config_iterator_t i, next;
	snd_pcm_alsa_dsp_t *alsa_dsp;
	int err;
	int ret;
	DENTER();

	/* Allocate the structure */
	alsa_dsp = calloc(1, sizeof(snd_pcm_alsa_dsp_t));
	if (alsa_dsp == NULL) {
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
		if (strcmp(id, "playback_device_file") == 0) {
			if (snd_config_get_type(n) == SND_CONFIG_TYPE_COMPOUND){
				if ((err = 
				     fill_string_list(n,
				          &(alsa_dsp->playback_devices))) < 0) {
					SNDERR("Could not fill string"
						" list for playback devices\n");
					goto error;
				}
			} else {
				SNDERR("Invalid type for %s", id);
				err = -EINVAL;
				goto error;
			}

			continue;
		}
		if (strcmp(id, "recording_device_file") == 0) {
			if (snd_config_get_type(n) == SND_CONFIG_TYPE_COMPOUND){
				if ((err =
				     fill_string_list(n,
					  &(alsa_dsp->recording_devices))) < 0){
					SNDERR("Could not fill string"
					       " list for recording devices\n");
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
	/* Initialise the dsp_protocol and create connection */
	if ((err = dsp_protocol_create(&(alsa_dsp->dsp_protocol))) < 0)
		goto error;
	if ((err = alsa_dsp_open_dsp_task(alsa_dsp,
					  (stream == SND_PCM_STREAM_PLAYBACK) ?
					  &(alsa_dsp->playback_devices) : 
					  &(alsa_dsp->recording_devices))) < 0)
		goto error;
	/* Initialise the snd_pcm_ioplug_t */
	alsa_dsp->io.version = SND_PCM_IOPLUG_VERSION;
	alsa_dsp->io.name = "Alsa - DSP PCM Plugin";
	alsa_dsp->io.mmap_rw = 0;
	alsa_dsp->io.callback = &alsa_dsp_callback;
	alsa_dsp->io.poll_fd = alsa_dsp->dsp_protocol->fd;
	alsa_dsp->io.poll_events = stream == SND_PCM_STREAM_PLAYBACK ?
	    POLLOUT : POLLIN;

	alsa_dsp->io.private_data = alsa_dsp;
	free_ref = alsa_dsp;

	if ((err = snd_pcm_ioplug_create(&alsa_dsp->io, name,
					 stream, mode)) < 0)
		goto error;

	/* Configure the plugin */
	if ((err = alsa_dsp_configure_constraints(alsa_dsp)) < 0) {
		snd_pcm_ioplug_delete(&alsa_dsp->io);
		goto error;
	}
	*pcmp = alsa_dsp->io.pcm;
	ret = 0;
	goto out;
      error:
	ret = err;
	free(alsa_dsp);
      out:
	DLEAVE(ret);
	return ret;
}


static void alsa_dsp_descructor(void) __attribute__ ((destructor));

static void alsa_dsp_descructor(void)
{
	DENTER();
	DPRINT("alsa dsp destructor\n");
	DPRINT("checking for memories leaks and releasing resources\n");
	if (free_ref) {
		if (free_ref->dsp_protocol) {
			dsp_protocol_close_node(free_ref->dsp_protocol);
			dsp_protocol_destroy(&(free_ref->dsp_protocol));	
		}
		free_device_list(&(free_ref->playback_devices));

		free_device_list(&(free_ref->recording_devices));
		
		free(free_ref);
		free_ref = NULL;
	}
	DLEAVE(0);

}

SND_PCM_PLUGIN_SYMBOL(alsa_dsp);
