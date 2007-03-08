asound_module_rate_lavcrate_LTLIBRARIES = libasound_module_rate_lavcrate.la

asound_module_rate_lavcratedir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @AVCODEC_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_rate_lavcrate_la_SOURCES = rate_lavcrate.c
libasound_module_rate_lavcrate_la_LIBADD = @ALSA_LIBS@ @AVCODEC_LIBS@

noinst_HEADERS = gcd.h

install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_*.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_higher.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_high.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_fast.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_faster.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_lavcrate_*.so
