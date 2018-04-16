GCONF_FILES = 50-pulseaudio.conf
LCONF_FILES = 99-pulseaudio-default.conf.example

EXTRA_DIST = $(GCONF_FILES) $(LCONF_FILES)

asound_module_pcm_LTLIBRARIES = libasound_module_pcm_pulse.la
asound_module_ctl_LTLIBRARIES = libasound_module_ctl_pulse.la
asound_module_conf_LTLIBRARIES = libasound_module_conf_pulse.la
asound_module_gconf_DATA = $(GCONF_FILES)
asound_module_lconf_DATA = $(LCONF_FILES)

asound_module_pcmdir = @ALSA_PLUGIN_DIR@
asound_module_ctldir = @ALSA_PLUGIN_DIR@
asound_module_confdir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@
asound_module_lconfdir = @ALSA_LCONF_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(PTHREAD_CFLAGS) $(pulseaudio_CFLAGS) -D_GNU_SOURCE
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_pulse_la_SOURCES = pcm_pulse.c pulse.c pulse.h
libasound_module_pcm_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

libasound_module_ctl_pulse_la_SOURCES = ctl_pulse.c pulse.c pulse.h
libasound_module_ctl_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

libasound_module_conf_pulse_la_SOURCES = conf_pulse.c
libasound_module_conf_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

include ../install-hooks.am

install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
