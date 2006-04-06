if HAVE_JACK
JACKDIR = jack
endif
if HAVE_POLYP
POLYPDIR = polyp
endif
if HAVE_SAMPLERATE
SAMPLERATEDIR = rate
endif
if HAVE_AVCODEC
A52DIR = a52
endif

SUBDIRS = oss mix $(JACKDIR) $(POLYPDIR) $(SAMPLERATEDIR) $(A52DIR) doc
EXTRA_DIST = cvscompile version COPYING.GPL
AUTOMAKE_OPTIONS = foreign

dist-hook:
	-chmod -R a+r $(distdir)
	@if ! test -z "$(AMTAR)"; then \
		$(AMTAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	else \
		$(TAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	fi
