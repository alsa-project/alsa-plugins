asound_module_rate_speexrate_LTLIBRARIES = libasound_module_rate_speexrate.la

asound_module_rate_speexratedir = $(libdir)/alsa-lib

AM_CFLAGS = -DVAR_ARRAYS -DRANDOM_PREFIX=alsa_lib -DOUTSIDE_SPEEX -Wall -g @ALSA_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-dynamic

libasound_module_rate_speexrate_la_SOURCES = rate_speexrate.c resample.c
libasound_module_rate_speexrate_la_LIBADD = @ALSA_LIBS@

install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_speexrate_*.so
	$(LN_S) libasound_module_rate_speexrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_speexrate_best.so
	$(LN_S) libasound_module_rate_speexrate.so $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_speexrate_medium.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/alsa-lib/libasound_module_rate_speexrate_*.so

noinst_HEADERS = speex_resampler.h arch.h fixed_generic.h
