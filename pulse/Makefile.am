asound_module_pcm_LTLIBRARIES = libasound_module_pcm_pulse.la
asound_module_ctl_LTLIBRARIES = libasound_module_ctl_pulse.la

asound_module_pcmdir = $(libdir)/alsa-lib
asound_module_ctldir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(PTHREAD_CFLAGS) $(pulseaudio_CFLAGS) -D_GNU_SOURCE
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_pcm_pulse_la_SOURCES = pcm_pulse.c pulse.c pulse.h
libasound_module_pcm_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)

libasound_module_ctl_pulse_la_SOURCES = ctl_pulse.c pulse.c pulse.h
libasound_module_ctl_pulse_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(pulseaudio_LIBS)