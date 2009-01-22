asound_module_ctl_arcam_av_LTLIBRARIES = libasound_module_ctl_arcam_av.la

asound_module_ctl_arcam_avdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined

libasound_module_ctl_arcam_av_la_SOURCES = ctl_arcam_av.c arcam_av.c arcam_av.h
libasound_module_ctl_arcam_av_la_LIBADD = @ALSA_LIBS@
