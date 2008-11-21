asound_module_pcm_oss_LTLIBRARIES = libasound_module_pcm_oss.la
asound_module_ctl_oss_LTLIBRARIES = libasound_module_ctl_oss.la

asound_module_pcm_ossdir = @ALSA_PLUGIN_DIR@
asound_module_ctl_ossdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_oss_la_SOURCES = pcm_oss.c
libasound_module_pcm_oss_la_LIBADD = @ALSA_LIBS@

libasound_module_ctl_oss_la_SOURCES = ctl_oss.c
libasound_module_ctl_oss_la_LIBADD = @ALSA_LIBS@
