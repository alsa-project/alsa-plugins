asound_module_rate_samplerate_LTLIBRARIES = libasound_module_rate_samplerate.la

asound_module_rate_sampleratedir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(samplerate_CFLAGS)
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_rate_samplerate_la_SOURCES = rate_samplerate.c
libasound_module_rate_samplerate_la_LIBADD = @ALSA_LIBS@ @samplerate_LIBS@

install-exec-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_samplerate_*.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_samplerate_best.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_samplerate_medium.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_samplerate_order.so
	$(LN_S) libasound_module_rate_samplerate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_samplerate_linear.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_samplerate_*.so
