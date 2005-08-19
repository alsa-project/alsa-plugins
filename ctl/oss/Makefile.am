asound_module_ctl_oss_LTLIBRARIES = libasound_module_ctl_oss.la

asound_module_ctl_ossdir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
libasound_module_ctl_oss_la_SOURCES = ctl_oss.c
libasound_module_ctl_oss_la_LDFLAGS = -module -avoid-version -export-dynamic
libasound_module_ctl_oss_la_LIBADD = @ALSA_LIBS@

#EXTRA_DIST = README
