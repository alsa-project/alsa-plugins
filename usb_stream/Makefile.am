asound_module_pcm_usb_stream_LTLIBRARIES = libasound_module_pcm_usb_stream.la

asound_module_pcm_usb_streamdir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic $(LDFLAGS_NOUNDEFINED)

libasound_module_pcm_usb_stream_la_SOURCES = pcm_usb_stream.c
libasound_module_pcm_usb_stream_la_LIBADD = @ALSA_LIBS@

noinst_HEADERS = usb_stream.h
