if HAVE_JACK
JACKDIR = jack
endif
if HAVE_POLYP
POLYPDIR = polyp
endif

SUBDIRS = oss $(JACKDIR) $(POLYPDIR) doc
EXTRA_DIST = cvscompile version
AUTOMAKE_OPTIONS = foreign

dist-hook:
	-chmod -R a+r $(distdir)
	@if ! test -z "$(AMTAR)"; then \
		$(AMTAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	else \
		$(TAR) --create --verbose --file=- $(distdir) | bzip2 -c -9 > $(distdir).tar.bz2 ; \
	fi
