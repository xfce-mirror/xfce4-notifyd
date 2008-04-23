
bin_PROGRAMS = xfce4-notifyd-config

xfce4_notifyd_config_SOURCES = \
	main.c \
	xfce4-notifyd-config.glade.h

xfce4_notifyd_config_CFLAGS = \
	-DLOCALEDIR=\"$(localedir)\" \
	$(XFCONF_CFLAGS) \
	$(GTK_CFLAGS) \
	$(LIBGLADE_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(LIBXFCEGUI4_CFLAGS) \
	$(DBUS_GLIB_CFLAGS)

xfce4_notifyd_config_LDADD = \
	$(XFCONF_LIBS) \
	$(GTK_LIBS) \
	$(LIBGLADE_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCEGUI4_LIBS) \
	$(DBUS_GLIB_LIBS)

desktopdir = $(datadir)/applications
desktop_in_files = xfce4-notifyd-config.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@


if MAINTAINER_MODE

BUILT_SOURCES = \
	xfce4-notifyd-config.glade.h

xfce4-notifyd-config.glade.h: $(srcdir)/xfce4-notifyd-config.glade
	exo-csource --static --name=xfce4_notifyd_config_glade $< >$@

endif


CLEANFILES = \
	$(BUILT_SOURCES) \
	$(desktop_DATA)

EXTRA_DIST = \
	xfce4-notifyd-config.glade
