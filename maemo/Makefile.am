GCONF_FILES = 98-maemo.conf

EXTRA_DIST = $(GCONF_FILES)

asound_module_pcm_alsa_dsp_LTLIBRARIES = libasound_module_pcm_alsa_dsp.la
asound_module_ctl_dsp_ctl_LTLIBRARIES = libasound_module_ctl_dsp_ctl.la
asound_module_gconf_DATA = $(GCONF_FILES)

asound_module_pcm_alsa_dspdir = @ALSA_PLUGIN_DIR@
asound_module_ctl_dsp_ctldir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@

AM_CFLAGS = -Wall -O2 @ALSA_CFLAGS@ $(DBUS_CFLAGS)
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_alsa_dsp_la_SOURCES = dsp-protocol.c alsa-dsp.c
libasound_module_pcm_alsa_dsp_la_LIBADD = @ALSA_LIBS@ $(DBUS_LIBS) -lpthread 

libasound_module_ctl_dsp_ctl_la_SOURCES = dsp-protocol.c dsp-ctl.c
libasound_module_ctl_dsp_ctl_la_LIBADD = @ALSA_LIBS@ $(DBUS_LIBS) -lpthread 

noinst_HEADERS = constants.h debug.h dsp-protocol.h list.h reporting.h \
	         types.h

include ../install-hooks.am

install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
