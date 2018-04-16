EXTRA_DIST = 60-a52-encoder.conf

asound_module_pcm_a52_LTLIBRARIES = libasound_module_pcm_a52.la
asound_module_addon_DATA = 60-a52-encoder.conf

asound_module_pcm_a52dir = @ALSA_PLUGIN_DIR@
asound_module_addondir = @ALSA_ADDON_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @LIBAV_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_a52_la_SOURCES = pcm_a52.c
libasound_module_pcm_a52_la_LIBADD = @ALSA_LIBS@ @LIBAV_LIBS@ @LIBAV_CODEC_LIBS@
