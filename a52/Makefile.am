GCONF_FILES = 60-a52-encoder.conf

EXTRA_DIST = $(GCONF_FILES)

asound_module_pcm_a52_LTLIBRARIES = libasound_module_pcm_a52.la
asound_module_gconf_DATA = $(GCONF_FILES)

asound_module_pcm_a52dir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @LIBAV_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_a52_la_SOURCES = pcm_a52.c
libasound_module_pcm_a52_la_LIBADD = @ALSA_LIBS@ @LIBAV_LIBS@ @LIBAV_CODEC_LIBS@

include ../install-hooks.am

install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
