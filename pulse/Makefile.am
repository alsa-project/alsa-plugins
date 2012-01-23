EXTRA_DIST = 50-pulseaudio.conf 99-pulseaudio-default.conf.example

asound_module_pcm_LTLIBRARIES = libasound_module_pcm_pulse.la
asound_module_ctl_LTLIBRARIES = libasound_module_ctl_pulse.la
asound_module_conf_LTLIBRARIES = libasound_module_conf_pulse.la
asound_module_data_DATA = 50-pulseaudio.conf 99-pulseaudio-default.conf.example

asound_module_pcmdir = @ALSA_PLUGIN_DIR@
asound_module_ctldir = @ALSA_PLUGIN_DIR@
asound_module_confdir = @ALSA_PLUGIN_DIR@
asound_module_datadir = @ALSA_DATA_DIR@/alsa.conf.d

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(PTHREAD_CFLAGS) $(pulseaudio_CFLAGS) -D_GNU_SOURCE
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_pulse_la_SOURCES = pcm_pulse.c pulse.c pulse.h
libasound_module_pcm_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

libasound_module_ctl_pulse_la_SOURCES = ctl_pulse.c pulse.c pulse.h
libasound_module_ctl_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

libasound_module_conf_pulse_la_SOURCES = conf_pulse.c
libasound_module_conf_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)
