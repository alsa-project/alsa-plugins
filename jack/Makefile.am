asound_module_pcm_jack_LTLIBRARIES = libasound_module_pcm_jack.la

asound_module_pcm_jackdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @JACK_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_jack_la_SOURCES = pcm_jack.c
libasound_module_pcm_jack_la_LIBADD = @ALSA_LIBS@ @JACK_LIBS@
