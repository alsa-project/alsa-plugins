asound_module_pcm_alsa_dsp_LTLIBRARIES = libasound_module_pcm_alsa_dsp.la
asound_module_ctl_dsp_ctl_LTLIBRARIES = libasound_module_ctl_dsp_ctl.la

asound_module_pcm_alsa_dspdir = @ALSA_PLUGIN_DIR@
asound_module_ctl_dsp_ctldir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -fPIC -DPIC -O2 @ALSA_CFLAGS@ $(DBUS_CFLAGS)
AM_LDFLAGS = -shared -lm -ldl -lpthread -module -avoid-version -export-dynamic -no-undefined

libasound_module_pcm_alsa_dsp_la_SOURCES = dsp-protocol.c alsa-dsp.c
libasound_module_pcm_alsa_dsp_la_LIBADD = @ALSA_LIBS@ $(DBUS_LIBS)

libasound_module_ctl_dsp_ctl_la_SOURCES = dsp-protocol.c dsp-ctl.c
libasound_module_ctl_dsp_ctl_la_LIBADD = @ALSA_LIBS@ $(DBUS_LIBS)

noinst_HEADERS = constants.h debug.h dsp-protocol.h list.h reporting.h \
	         types.h
