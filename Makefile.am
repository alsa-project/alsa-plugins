SUBDIRS = oss mix usb_stream arcam-av doc
if HAVE_JACK
SUBDIRS += jack
endif
if HAVE_PULSE
SUBDIRS += pulse
endif
if HAVE_SAMPLERATE
SUBDIRS += rate
endif
if HAVE_AVCODEC
SUBDIRS += a52 rate-lavc
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
