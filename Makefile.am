if HAVE_JACK
JACKDIR = jack
endif
if HAVE_PULSE
PULSEDIR = pulse
endif
if HAVE_SAMPLERATE
SAMPLERATEDIR = rate
endif
if HAVE_AVCODEC
A52DIR = a52
LAVCRATEDIR = rate-lavc
endif
if HAVE_DBUS
MAEMODIR = maemo
endif

SUBDIRS = oss mix pph $(JACKDIR) $(PULSEDIR) $(SAMPLERATEDIR) $(A52DIR) $(LAVCRATEDIR) $(MAEMODIR) doc
EXTRA_DIST = hgcompile version COPYING.GPL
AUTOMAKE_OPTIONS = foreign

dist-hook:
	-chmod -R a+r $(distdir)
	@if ! test -z "$(AMTAR)"; then \
		$(AMTAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	else \
		$(TAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	fi
