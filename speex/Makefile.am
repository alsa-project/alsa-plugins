asound_module_pcm_speex_LTLIBRARIES = libasound_module_pcm_speex.la

asound_module_pcm_speexdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @speexdsp_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_speex_la_SOURCES = pcm_speex.c
libasound_module_pcm_speex_la_LIBADD = @ALSA_LIBS@ @speexdsp_LIBS@
