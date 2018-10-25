SUBDIRS = doc
if HAVE_OSS
SUBDIRS += oss
endif
if HAVE_MIX
SUBDIRS += mix
endif
if HAVE_JACK
SUBDIRS += jack
endif
if HAVE_PULSE
SUBDIRS += pulse
endif
if HAVE_SAMPLERATE
SUBDIRS += rate
endif
if HAVE_A52
SUBDIRS += a52
endif
if HAVE_LAVRATE
SUBDIRS += rate-lav
endif
if HAVE_USBSTREAM
SUBDIRS += usb_stream
endif
if HAVE_ARCAMAV
SUBDIRS += arcam-av
endif
if HAVE_MAEMO_PLUGIN
SUBDIRS += maemo
endif
if HAVE_PPH
SUBDIRS += pph
endif
if HAVE_SPEEXDSP
SUBDIRS += speex
endif
if HAVE_AAF
SUBDIRS += aaf
endif

EXTRA_DIST = gitcompile version COPYING.GPL m4/attributes.m4
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

dist-hook:
	-chmod -R a+r $(distdir)
	@if ! test -z "$(AMTAR)"; then \
		$(AMTAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	else \
		$(TAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	fi
