AC_PREREQ(2.59)
AC_INIT(alsa-plugins, 1.1.7)
AM_INIT_AUTOMAKE
AC_PREFIX_DEFAULT(/usr)

AM_MAINTAINER_MODE([enable])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_INSTALL
AC_DISABLE_STATIC
AM_PROG_LIBTOOL
AC_HEADER_STDC

CC_NOUNDEFINED

PKG_CHECK_MODULES(ALSA, alsa >= 1.1.6)
AC_CHECK_LIB(asound, snd_pcm_ioplug_create,,
	     AC_ERROR([*** libasound has no external plugin SDK]), -ldl)

AC_ARG_ENABLE([oss],
      AS_HELP_STRING([--disable-oss], [Disable building of OSS plugin]))
AS_IF([test "x$enable_oss" != "xno"],
      [AM_CONDITIONAL(HAVE_OSS, true)],
      [AM_CONDITIONAL(HAVE_OSS, false)])

AC_ARG_ENABLE([mix],
      AS_HELP_STRING([--disable-mix], [Disable building of upmix and vdownmix plugins]))
AS_IF([test "x$enable_mix" != "xno"],
      [AM_CONDITIONAL(HAVE_MIX, true)],
      [AM_CONDITIONAL(HAVE_MIX, false)])

AC_ARG_ENABLE([usbstream],
      AS_HELP_STRING([--disable-usbstream], [Disable building of usb_stream plugin]))
AS_IF([test "x$enable_usbstream" != "xno"],
      [AM_CONDITIONAL(HAVE_USBSTREAM, true)],
      [AM_CONDITIONAL(HAVE_USBSTREAM, false)])

AC_ARG_ENABLE([arcamav],
      AS_HELP_STRING([--disable-arcamav], [Disable building of Arcam AV control plugin]))
AS_IF([test "x$enable_arcamav" != "xno"],
      [AM_CONDITIONAL(HAVE_ARCAMAV, true)],
      [AM_CONDITIONAL(HAVE_ARCAMAV, false)])

AC_ARG_ENABLE([jack],
      AS_HELP_STRING([--disable-jack], [Disable building of JACK plugin]))

if test "x$enable_jack" != "xno"; then
  PKG_CHECK_MODULES(JACK, jack >= 0.98, [HAVE_JACK=yes], [HAVE_JACK=no])
fi
AM_CONDITIONAL(HAVE_JACK, test x$HAVE_JACK = xyes)

AC_ARG_ENABLE([pulseaudio],
      AS_HELP_STRING([--disable-pulseaudio], [Disable building of pulseaudio plugin]))

if test "x$enable_pulseaudio" != "xno"; then
  PKG_CHECK_MODULES(pulseaudio, [libpulse >= 0.9.11], [HAVE_PULSE=yes], [HAVE_PULSE=no])
fi
AM_CONDITIONAL(HAVE_PULSE, test x$HAVE_PULSE = xyes)

AC_ARG_ENABLE([samplerate],
      AS_HELP_STRING([--disable-samplerate], [Disable building of samplerate plugin]))

if test "x$enable_samplerate" != "xno"; then
  PKG_CHECK_MODULES(samplerate, [samplerate], [HAVE_SAMPLERATE=yes], [HAVE_SAMPLERATE=no])
fi
AM_CONDITIONAL(HAVE_SAMPLERATE, test x$HAVE_SAMPLERATE = xyes)

AC_ARG_ENABLE([maemo-plugin],
	AS_HELP_STRING([--enable-maemo-plugin],
		[Build Maemo DSP plugin]),
	[HAVE_MAEMO_PLUGIN="$enableval"], [HAVE_MAEMO_PLUGIN="no"])
AM_CONDITIONAL(HAVE_MAEMO_PLUGIN, test x$HAVE_MAEMO_PLUGIN = xyes)

AC_ARG_ENABLE([maemo-resource-manager],
	AS_HELP_STRING([--enable-maemo-resource-manager],
		[Use Maemo resource manager]),
	[use_maemo_rm="$enableval"], [use_maemo_rm="no"])

if test "$use_maemo_rm" = "yes"; then
        PKG_CHECK_MODULES(DBUS, [dbus-1], [HAVE_DBUS=yes], [HAVE_DBUS=no])

        if test "$HAVE_DBUS" = "yes"; then
                AC_DEFINE(USE_RESOURCE_MANAGER, 1,"Use dbus server as a resouce manager")
        else
        	AC_ERROR([You must have D-Bus installed if you want to enable the Maemo resource manager])
        fi
fi

AC_ARG_ENABLE([libav],
      AS_HELP_STRING([--disable-libav], [Do not build plugins depending on libav/ffmpeg (a52,lavrate...)]))

if test "x$enable_libav" != "xno"; then
  PKG_CHECK_MODULES(LIBAV, [libavcodec libavutil libavresample], [HAVE_LIBAV=yes], [HAVE_LIBAV=no])
fi

if test "x$HAVE_LIBAV" = "xno"; then
  AC_ARG_WITH([libav-includedir],
	AS_HELP_STRING([--with-libav-includedir=dir],
		[Libav/ffmpeg include directory]),
		[LIBAV_CFLAGS="-I$(withval)"], [LIBAV_CFLAGS=""])
  AC_ARG_WITH([libav-libdir],
	AS_HELP_STRING([--with-libav-libdir=dir],
		[Libav/ffmpeg library directory]),
		[LIBAV_LIBS="-L$withval"], [LIBAV_LIBS=""])

  CFLAGS_saved="$CFLAGS"
  LDFLAGS_saved="$LDFLAGS"
  CFLAGS="$CFLAGS $LIBAV_CFLAGS"
  LDFLAGS="$LDFLAGS $LIBAV_LIBS"
  AC_CHECK_LIB([avcodec], [avcodec_open], [HAVE_LIBAV=yes], [HAVE_LIBAV=no])
  CFLAGS="$CFLAGS_saved"
  LDFLAGS="$LDFLAGS_saved"
  LIBAV_CODEC_LIBS="-lavcodec"
  LIBAV_RESAMPLE_LIBS="-lavresample -lavutil"
fi

AM_CONDITIONAL(HAVE_LIBAV, test x$HAVE_LIBAV = xyes)
AC_SUBST(LIBAV_CFLAGS)
AC_SUBST(LIBAV_LIBS)
AC_SUBST(LIBAV_CODEC_LIBS)
AC_SUBST(LIBAV_RESAMPLE_LIBS)

AC_ARG_ENABLE([a52],
      AS_HELP_STRING([--disable-a52], [Disable building of A52 encoder plugin]))

if test "x$enable_a52" != "xno" -a "$HAVE_LIBAV" = "yes"; then
  HAVE_A52=yes
fi
AM_CONDITIONAL(HAVE_A52, test x$HAVE_A52 = xyes)

AC_ARG_ENABLE([lavrate],
      AS_HELP_STRING([--disable-lavrate], [Disable building of libav/ffmpeg rate plugin]))

if test "x$enable_lavrate" != "xno" -a "$HAVE_LIBAV" = "yes"; then
  HAVE_LAVRATE=yes
fi
AM_CONDITIONAL(HAVE_LAVRATE, test x$HAVE_LAVRATE = xyes)

AC_ARG_ENABLE([speexdsp],
      AS_HELP_STRING([--disable-speexdsp], [Disable building of speexdsp plugin]))

if test "x$enable_speexdsp" != "xno"; then
  PKG_CHECK_MODULES(speexdsp, [speexdsp >= 1.2rc2], [HAVE_SPEEXDSP="yes"], [HAVE_SPEEXDSP=""])
fi
AM_CONDITIONAL(HAVE_SPEEXDSP, test "$HAVE_SPEEXDSP" = "yes")

AC_ARG_WITH([speex],
	AS_HELP_STRING([--with-speex={builtin|lib|no}],
		[build speex resampler (built-in code, link with external lib, or no build)]),
	[PPH=$withval], [PPH="lib"])

USE_LIBSPEEX=""
if test "$PPH" = "lib"; then
	if test "$HAVE_SPEEXDSP" = "yes"; then
		AC_CHECK_LIB([speexdsp], [speex_resampler_init],
			[USE_LIBSPEEX="yes"], [USE_LIBSPEEX=""])
	fi
	if test "$USE_LIBSPEEX" = "yes"; then
		AC_DEFINE(USE_LIBSPEEX, 1, "Link with libspeex for resampler")
	else
		echo "No libspeex with resampler unit; use built-in code"
		PPH="builtin"
	fi
fi

AM_CONDITIONAL(HAVE_PPH, test "$PPH" = "builtin" -o "$PPH" = "lib")
AM_CONDITIONAL(USE_LIBSPEEX, test "$PPH" = "lib")

if test "$PPH" = "lib"; then
    AC_CHECK_HEADERS([speex/speexdsp_types.h])
fi

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix=$prefix

AC_ARG_ENABLE([aaf],
      AS_HELP_STRING([--disable-aaf], [Disable building of AAF plugin]))

if test "x$enable_aaf" != "xno"; then
  PKG_CHECK_MODULES(AVTP, avtp >= 0.1, [HAVE_AAF=yes], [HAVE_AAF=no])
  AC_CHECK_HEADERS([linux/if_ether.h], [], [HAVE_AAF=no])
fi
AM_CONDITIONAL(HAVE_AAF, test x$HAVE_AAF = xyes)

dnl ALSA plugin directory
AC_ARG_WITH(plugindir,
    AS_HELP_STRING([--with-plugindir=dir],
	[path where ALSA plugin files are stored]),
    plugindir="$withval", plugindir="")
if test -z "$plugindir"; then
    eval dir="$libdir"
    case "$dir" in
    /*) ;;
    *) dir="$dir"
    esac
    plugindir="$dir/alsa-lib"
fi
AC_DEFINE_UNQUOTED(ALSA_PLUGIN_DIR, "$plugindir", [directory containing ALSA add-on modules])
ALSA_PLUGIN_DIR="$plugindir"
AC_SUBST(ALSA_PLUGIN_DIR)

dnl ALSA data directory
AC_ARG_WITH(alsadatadir,
    AS_HELP_STRING([--with-alsadatadir=dir],
	[path where ALSA data files are stored]),
    alsadatadir="$withval", alsadatadir="")
if test -z "$alsadatadir"; then
    eval dir="$datadir"
    case "$dir" in
    /*) ;;
    *) dir="$dir"
    esac
    alsadatadir="$dir/alsa"
fi
AC_DEFINE_UNQUOTED(ALSA_DATA_DIR, "$alsadatadir", [directory containing ALSA data files])
ALSA_DATA_DIR="$alsadatadir"
AC_SUBST(ALSA_DATA_DIR)

dnl ALSA add-on global config directory
AC_ARG_WITH(alsagconfdir,
    AS_HELP_STRING([--with-alsagconfdir=dir],
	[path where ALSA global add-on config files are stored]),
    alsagconfdir="$withval", alsagconfdir="")
if test -z "$alsagconfdir"; then
    alsagconfdir="$ALSA_DATA_DIR/alsa.conf.d"
fi
AC_DEFINE_UNQUOTED(ALSA_GCONF_DIR, "$alsagconfdir", [directory containing global ALSA add-on config files])
ALSA_GCONF_DIR="$alsagconfdir"
AC_SUBST(ALSA_GCONF_DIR)

dnl ALSA add-on local config directory
AC_ARG_WITH(alsalconfdir,
    AS_HELP_STRING([--with-alsalconfdir=dir],
	[path where ALSA local add-on config files are stored]),
    alsalconfdir="$withval", alsalconfdir="")
if test -z "$alsalconfdir"; then
    alsalconfdir="/etc/alsa/conf.d"
fi
AC_DEFINE_UNQUOTED(ALSA_LCONF_DIR, "$alsalconfdir", [directory containing local ALSA add-on config files])
ALSA_LCONF_DIR="$alsalconfdir"
AC_SUBST(ALSA_LCONF_DIR)

SAVE_PLUGINS_VERSION

AC_OUTPUT([
	Makefile
	oss/Makefile
	pph/Makefile
	jack/Makefile
	pulse/Makefile
	mix/Makefile
	rate/Makefile
	a52/Makefile
	rate-lav/Makefile
	maemo/Makefile
	doc/Makefile
	usb_stream/Makefile
	speex/Makefile
	arcam-av/Makefile
	aaf/Makefile
])

dnl Show the build conditions

echo
echo "Plugin directory: $ALSA_PLUGIN_DIR"
echo "ALSA_CFLAGS: $ALSA_CFLAGS"
echo "ALSA_LIBS: $ALSA_LIBS"
echo "JACK plugin:        $HAVE_JACK"
if test "$HAVE_JACK" = "yes"; then
  echo "  JACK_CFLAGS: $JACK_CFLAGS"
  echo "  JACK_LIBS: $JACK_LIBS"
fi
echo "Pulseaudio plugin:  $HAVE_PULSE"
if test "$HAVE_PULSE" = "yes"; then
  echo "  pulseaudio_CFLAGS: $pulseaudio_CFLAGS"
  echo "  pulseaudio_LIBS: $pulseaudio_LIBS"
fi
echo "Samplerate plugin:  $HAVE_SAMPLERATE"
if test "$HAVE_SAMPLERATE" = "yes"; then
  echo "  samplerate_CFLAGS: $samplerate_CFLAGS"
  echo "  samplerate_LIBS: $samplerate_LIBS"
fi
echo "Maemo plugin:       $HAVE_MAEMO_PLUGIN"
echo "  Using Osso resource manager: $use_maemo_rm"
if test "$HAVE_LIBAV" = "yes"; then
  echo "Libav/ffmpeg config:"
  echo "  LIBAV_CFLAGS:   $LIBAV_CFLAGS"
  echo "  LIBAV_LIBS:     $LIBAV_LIBS / $LIBAV_CODEC_LIBS / $LIBAV_RESAMPLE_LIBS"
fi
echo "Libav A52 plugin:   $HAVE_A52"
echo "Libav rate plugin:  $HAVE_LAVRATE"
echo "Speex rate plugin:  $PPH"
echo "Speex preprocess plugin:  $HAVE_SPEEXDSP"
if test "$HAVE_SPEEX" = "yes"; then
  echo "  speexdsp_CFLAGS: $speexdsp_CFLAGS"
  echo "  speexdsp_LIBS: $speexdsp_LIBS"
fi
echo "AAF plugin:         $HAVE_AAF"
