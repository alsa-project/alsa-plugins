asound_module_pcm_upmix_LTLIBRARIES = libasound_module_pcm_upmix.la
asound_module_pcm_vdownmix_LTLIBRARIES = libasound_module_pcm_vdownmix.la

asound_module_pcm_upmixdir = @ALSA_PLUGIN_DIR@
asound_module_pcm_vdownmixdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_upmix_la_SOURCES = pcm_upmix.c
libasound_module_pcm_upmix_la_LIBADD = @ALSA_LIBS@
libasound_module_pcm_vdownmix_la_SOURCES = pcm_vdownmix.c
libasound_module_pcm_vdownmix_la_LIBADD = @ALSA_LIBS@

