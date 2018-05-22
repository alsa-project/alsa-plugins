GCONF_FILES = 50-arcam-av-ctl.conf

EXTRA_DIST = $(GCONF_FILES)

asound_module_ctl_arcam_av_LTLIBRARIES = libasound_module_ctl_arcam_av.la
asound_module_gconf_DATA = $(GCONF_FILES)

asound_module_ctl_arcam_avdir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined

libasound_module_ctl_arcam_av_la_SOURCES = ctl_arcam_av.c arcam_av.c arcam_av.h
libasound_module_ctl_arcam_av_la_LIBADD = @ALSA_LIBS@ -lpthread

include ../install-hooks.am

install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
