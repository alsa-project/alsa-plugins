asound_module_pcm_upmix_LTLIBRARIES = libasound_module_pcm_upmix.la
asound_module_pcm_vdownmix_LTLIBRARIES = libasound_module_pcm_vdownmix.la

asound_module_pcm_upmixdir = $(libdir)/alsa-lib
asound_module_pcm_vdownmixdir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_pcm_upmix_la_SOURCES = pcm_upmix.c
libasound_module_pcm_upmix_la_LIBADD = @ALSA_LIBS@
libasound_module_pcm_vdownmix_la_SOURCES = pcm_vdownmix.c
libasound_module_pcm_vdownmix_la_LIBADD = @ALSA_LIBS@

