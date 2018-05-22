GCONF_FILES = 98-usb-stream.conf

EXTRA_DIST = $(GCONF_FILES)

asound_module_pcm_usb_stream_LTLIBRARIES = libasound_module_pcm_usb_stream.la
asound_module_gconf_DATA = $(GCONF_FILES)

asound_module_pcm_usb_streamdir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_usb_stream_la_SOURCES = pcm_usb_stream.c
libasound_module_pcm_usb_stream_la_LIBADD = @ALSA_LIBS@ -lpthread

noinst_HEADERS = usb_stream.h

include ../install-hooks.am

install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
