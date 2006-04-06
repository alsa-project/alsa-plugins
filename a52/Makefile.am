asound_module_pcm_a52_LTLIBRARIES = libasound_module_pcm_a52.la

asound_module_pcm_a52dir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @AVCODEC_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_pcm_a52_la_SOURCES = pcm_a52.c
libasound_module_pcm_a52_la_LIBADD = @ALSA_LIBS@ @AVCODEC_LIBS@
