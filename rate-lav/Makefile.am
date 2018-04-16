GCONF_FILES = 10-rate-lav.conf

EXTRA_DIST = $(GCONF_FILES)

asound_module_rate_lavrate_LTLIBRARIES = libasound_module_rate_lavrate.la
asound_module_gconf_DATA = $(GCONF_FILES)

asound_module_rate_lavratedir = @ALSA_PLUGIN_DIR@
asound_module_gconfdir = @ALSA_GCONF_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ @LIBAV_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_rate_lavrate_la_SOURCES = rate_lavrate.c
libasound_module_rate_lavrate_la_LIBADD = @ALSA_LIBS@ @LIBAV_LIBS@ @LIBAV_RESAMPLE_LIBS@

noinst_HEADERS = gcd.h

include ../install-hooks.am

install-exec-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_*.so
	$(LN_S) libasound_module_rate_lavrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_higher.so
	$(LN_S) libasound_module_rate_lavrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_high.so
	$(LN_S) libasound_module_rate_lavrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_fast.so
	$(LN_S) libasound_module_rate_lavrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_faster.so

uninstall-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_lavrate_*.so
install-data-hook: install-conf-hook

uninstall-local: uninstall-conf-hook
