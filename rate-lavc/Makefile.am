asound_module_rate_lavcrate_LTLIBRARIES = libasound_module_rate_lavcrate.la

asound_module_rate_lavcratedir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @AVCODEC_CFLAGS@ \
	-DAVCODEC_HEADER="@AVCODEC_HEADER@"
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_rate_lavcrate_la_SOURCES = rate_lavcrate.c
libasound_module_rate_lavcrate_la_LIBADD = @ALSA_LIBS@ @AVCODEC_LIBS@

noinst_HEADERS = gcd.h

install-exec-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_*.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_higher.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_high.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_fast.so
	$(LN_S) libasound_module_rate_lavcrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_faster.so

uninstall-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavcrate_*.so
