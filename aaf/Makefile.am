asound_module_pcm_aaf_LTLIBRARIES = libasound_module_pcm_aaf.la

asound_module_pcm_aafdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = @ALSA_CFLAGS@ @AVTP_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_aaf_la_SOURCES = pcm_aaf.c
libasound_module_pcm_aaf_la_LIBADD = @ALSA_LIBS@ @AVTP_LIBS@
