asound_module_rate_samplerate_LTLIBRARIES = libasound_module_rate_samplerate.la

asound_module_rate_sampleratedir = $(libdir)/alsa-lib

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(samplerate_CFLAGS)
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_rate_samplerate_la_SOURCES = rate_samplerate.c
libasound_module_rate_samplerate_la_LIBADD = @ALSA_LIBS@ @samplerate_LIBS@

install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_*.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_best.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_medium.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_order.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_linear.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_*.so
