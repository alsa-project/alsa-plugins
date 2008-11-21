asound_module_pcm_a52_LTLIBRARIES = libasound_module_pcm_a52.la

asound_module_pcm_a52dir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @AVCODEC_CFLAGS@ \
	-DAVCODEC_HEADER="@AVCODEC_HEADER@"
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_a52_la_SOURCES = pcm_a52.c
libasound_module_pcm_a52_la_LIBADD = @ALSA_LIBS@ @AVCODEC_LIBS@
