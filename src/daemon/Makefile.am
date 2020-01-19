
localedir = $(datadir)/locale

INCLUDES = -I$(top_builddir) \
	-I$(top_srcdir) \
	$(SEAHORSE_CFLAGS) \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DPIXMAPSDIR=\""$(pixmapsdir)"\" \
	-DAUTOSTARTDIR=\""$(sysconfdir)/xdg/autostart"\"

bin_PROGRAMS = seahorse-sharing

seahorse_sharing_SOURCES = \
	seahorse-hkp-server.c seahorse-sharing.c \
	seahorse-daemon.c seahorse-daemon.h \
	seahorse-unix-signal.c seahorse-unix-signal.h

seahorse_sharing_LDADD = \
	$(top_builddir)/libegg/libeggsmclient.la \
	$(SEAHORSE_LIBS)

man_MANS = seahorse-sharing.1

#autostart
desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = seahorse-sharing.desktop.in 
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = \
	$(man_MANS) \
	seahorse-sharing.desktop.in

CLEANFILES = *.bak \
	seahorse-sharing.desktop
