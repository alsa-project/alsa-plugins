asound_module_rate_speexrate_LTLIBRARIES = libasound_module_rate_speexrate.la

asound_module_rate_speexratedir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -DVAR_ARRAYS -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_rate_speexrate_la_SOURCES = rate_speexrate.c
libasound_module_rate_speexrate_la_LIBADD = @ALSA_LIBS@
if USE_LIBSPEEX
AM_CFLAGS += @speexdsp_CFLAGS@
libasound_module_rate_speexrate_la_LIBADD += @speexdsp_LIBS@
else
AM_CFLAGS += -DRANDOM_PREFIX=alsa_lib -DOUTSIDE_SPEEX 
libasound_module_rate_speexrate_la_SOURCES += resample.c
libasound_module_rate_speexrate_la_LIBADD += -lm
endif

install-exec-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_speexrate_*.so
	$(LN_S) libasound_module_rate_speexrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_speexrate_best.so
	$(LN_S) libasound_module_rate_speexrate.so $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_speexrate_medium.so

uninstall-hook:
	rm -f $(DESTDIR)@ALSA_PLUGIN_DIR@/libasound_module_rate_speexrate_*.so

noinst_HEADERS = speex_resampler.h arch.h fixed_generic.h
