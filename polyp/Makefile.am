asound_module_pcm_LTLIBRARIES = libasound_module_pcm_polyp.la
asound_module_ctl_LTLIBRARIES = libasound_module_ctl_polyp.la

asound_module_pcmdir = $(libdir)/alsa-lib
asound_module_ctldir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(PTHREAD_CFLAGS) $(polypaudio_CFLAGS) -D_GNU_SOURCE
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_pcm_polyp_la_SOURCES = pcm_polyp.c polyp.c polyp.h
libasound_module_pcm_polyp_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(polypaudio_LIBS)

libasound_module_ctl_polyp_la_SOURCES = ctl_polyp.c polyp.c polyp.h
libasound_module_ctl_polyp_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(polypaudio_LIBS)
