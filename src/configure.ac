dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.52)

dnl ****************************************************************************
dnl MAIN SEAHORSE VERSION (update after release)

AC_INIT(seahorse-sharing, 3.8.0)

dnl ****************************************************************************

AC_CONFIG_SRCDIR(daemon/seahorse-daemon.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INTLTOOL([0.35.0])
AM_PROG_LIBTOOL

dnl Get the off_t size right
AC_SYS_LARGEFILE

AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.0)
AC_SUBST([GTK_LIBS])
AC_SUBST([GTK_CFLAGS])

AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
PKG_CHECK_MODULES(SEAHORSE, gthread-2.0 gtk+-3.0 >= 3.0)

dnl **************************************************************
dnl GPG / GPGME CHECKS
dnl **************************************************************

have_gpg=""
have_gpgme=""

AC_ARG_ENABLE(gpg-check,
	AC_HELP_STRING([--disable-gpg-check], [check GPG version (default is yes)]),
	DO_CHECK=$enableval, DO_CHECK=yes)

if test	"$DO_CHECK" = "yes"; then
	accepted_versions="1.2 1.4 2.0"
	AC_PATH_PROGS(GNUPG, [gpg gpg2], no)
	ok="no"
	if test "$GNUPG" != "no"; then
		AC_MSG_CHECKING(for appropriate GnuPG version)
		gnupg_version=`$GNUPG --version | grep gpg`
		major=`echo $gnupg_version | \
			sed 's/^gpg (GnuPG) \([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\1/'`
		minor=`echo $gnupg_version | \
			sed 's/^gpg (GnuPG) \([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\2/'`
		micro=`echo $gnupg_version | \
			sed 's/^gpg (GnuPG) \([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\3/'`

		for ver in $accepted_versions; do
			if test "$ver" = "$major.$minor"; then
				AC_DEFINE_UNQUOTED(GPG_MAJOR, [$major], [GPG Major Version])
				AC_DEFINE_UNQUOTED(GPG_MINOR, [$minor], [GPG Minor Version])
				AC_DEFINE_UNQUOTED(GPG_MICRO, [$micro], [GPG Micro Version])
				ok="yes"
				break
			fi
		done
	fi

	if test "$ok" = "yes"; then
		AC_MSG_RESULT([yes])
		have_gpg="$gnupg_version"
	else
		AC_MSG_RESULT([no])
	fi
fi


ok="no"
min_gpgme_version=1.0.0
AC_PATH_PROG(GPGME_CONFIG, gpgme-config, "failed")
if test $GPGME_CONFIG != "failed" ; then
	AC_MSG_CHECKING(for GPGME - version >= $min_gpgme_version)
	req_major=`echo $min_gpgme_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\)/\1/'`
	req_minor=`echo $min_gpgme_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\)/\2/'`
	req_micro=`echo $min_gpgme_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\)/\3/'`
	gpgme_config_version=`$GPGME_CONFIG --version`
	major=`echo $gpgme_config_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\1/'`
	minor=`echo $gpgme_config_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\2/'`
	micro=`echo $gpgme_config_version | \
		sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\.\([[0-9]]*\).*/\3/'`

	if test "$major" -eq "$req_major"; then
		if test "$minor" -ge "$req_minor"; then
			if test "$micro" -ge "$req_micro"; then
				ok="yes"
			fi
		fi
	fi
fi

if test $ok = "yes"; then
	GPGME_CFLAGS=`$GPGME_CONFIG --cflags`
	GPGME_LIBS=`$GPGME_CONFIG --libs`
	AC_MSG_RESULT(yes)
	have_gpgme="$gpgme_config_version"
else
	AC_MSG_RESULT(no)
fi

if test -z "$have_gpg"; then
	AC_MSG_ERROR([Appropriate version of GnuPG not found. Please install one of versions: $accepted_versions])
fi

if test -z "$have_gpgme"; then
	AC_MSG_ERROR(GPGME $min_gpgme_version or later needed)
fi

SEAHORSE_CFLAGS="$SEAHORSE_CFLAGS $GPGME_CFLAGS"
SEAHORSE_LIBS="$SEAHORSE_LIBS $GPGME_LIBS"

dnl ****************************************************************************
dnl HKP (libsoup)
dnl ****************************************************************************

echo "checking for supported versions of libsoup..."

PKG_CHECK_MODULES(SOUP, libsoup-2.4)
SEAHORSE_LIBS="$SEAHORSE_LIBS $SOUP_LIBS"
SEAHORSE_CFLAGS="$SEAHORSE_CFLAGS $SOUP_CFLAGS"

dnl *******************
dnl KEY SHARING
dnl *******************

AVAHI_GLIB_REQUIRED=0.6

PKG_CHECK_MODULES(SHARING, avahi-client avahi-glib >= $AVAHI_GLIB_REQUIRED)
SEAHORSE_CFLAGS="$SEAHORSE_CFLAGS $SHARING_CFLAGS"
SEAHORSE_LIBS="$SEAHORSE_LIBS $SHARING_LIBS"

dnl ****************************************************************************

# Debug mode
AC_ARG_ENABLE(debug,
		AC_HELP_STRING([--enable-debug],
		[Compile binaries in debug mode]))

if test "$enable_debug" = "yes"; then
	CFLAGS="$CFLAGS -g -O0 -DBONOBO_DISABLE_DEPRECATED -DG_DISABLE_DEPRECATED -Wall"
	AC_DEFINE_UNQUOTED(_DEBUG, 1, [In debug mode])
	echo "enabling debug compile mode"
else
	dnl AC_DEFINE_UNQUOTED(G_DISABLE_ASSERT, 1, [Disable glib assertions])
	echo "disabling debug compile mode"
fi

# Fatal Messages mode
AC_ARG_ENABLE(fatal-messages,
		AC_HELP_STRING([--enable-fatal-messages],
		[Don't make compile and or runtime messages fatal in debug mode.]))

if test "$enable_fatal_messages" = "yes" && test "$enable_debug" = "yes"; then
	CFLAGS="$CFLAGS -Wall -Werror"
	AC_DEFINE_UNQUOTED(FATAL_MESSAGES, 1, [Fatal runtime messages])
	echo "enabling fatal runtime messages"
fi

# Build Tests
AC_ARG_ENABLE(tests,
		AC_HELP_STRING([--enable-tests],
		[Compile Test Programs]))

if test "$enable_tests" != "yes"; then
	enable_tests="no"
fi

AM_CONDITIONAL(WITH_TESTS, test "$enable_tests" = "yes")

dnl ****************************************************************************

AC_SUBST(SEAHORSE_CFLAGS)
AC_SUBST(SEAHORSE_LIBS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl *******************
dnl ***** gettext *****
dnl *******************

GETTEXT_PACKAGE=seahorse-sharing
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of the gettext domain.])

AM_GLIB_GNU_GETTEXT

dnl ****************************************************************************
## here we get the flags we'll actually use
PKG_CHECK_MODULES(EGG, gtk+-3.0 >= 3.0)
AC_SUBST(EGG_LIBS)
AC_SUBST(EGG_CFLAGS)

PKG_CHECK_MODULES(EGG_SMCLIENT, gtk+-3.0)
AC_SUBST(EGG_SMCLIENT_LIBS)
AC_SUBST(EGG_SMCLIENT_CFLAGS)

dnl ****************************************************************************

AC_OUTPUT([
Makefile
daemon/Makefile
libegg/Makefile
po/Makefile.in
pixmaps/Makefile
pixmaps/22x22/Makefile
pixmaps/48x48/Makefile
pixmaps/scalable/Makefile
])

dnl ****************************************************************************
dnl  SUMMARY
dnl ****************************************************************************

echo "
GnuPG Version:              $have_gpg
GPGME Version:              $have_gpgme
"
