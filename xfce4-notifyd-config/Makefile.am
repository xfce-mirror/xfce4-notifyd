
bin_PROGRAMS = \
	xfce4-notifyd-config

xfce4_notifyd_config_SOURCES = \
	$(top_srcdir)/common/xfce-notify-common.h \
	main.c \
	xfce-notify-log-viewer.c \
	xfce-notify-log-viewer.h \
	xfce4-notifyd-config.ui.h \
	xfce4-notifyd-config-known-app.ui.h

xfce4_notifyd_config_CFLAGS = \
	-DG_LOG_DOMAIN=\"xfce4-notifyd-config\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(builddir) \
	$(LIBNOTIFY_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(GTK_CFLAGS) \
	$(SQLITE_CFLAGS)

xfce4_notifyd_config_LDADD = \
	$(top_builddir)/common/libxfce-notifyd-common.la \
	$(LIBXFCE4UTIL_LIBS) \
	$(XFCONF_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(GTK_LIBS) \
	$(SQLITE_LIBS) \
	$(LIBNOTIFY_LIBS)

dist_man1_MANS = \
	xfce4-notifyd-config.1

desktopdir = $(datadir)/applications
desktop_in_files = xfce4-notifyd-config.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

if MAINTAINER_MODE

built_xfce4_notifyd_config = \
	xfce4-notifyd-config.ui.h \
	xfce4-notifyd-config-known-app.ui.h

endif

dist_noinst_DATA = \
	xfce4-notifyd-config.glade \
	xfce4-notifyd-config-known-app.glade \
	$(desktop_in_files)

BUILT_SOURCES = \
	$(built_xfce4_notifyd_config)

CLEANFILES = \
	$(built_xfce4_notifyd_config) \
	$(desktop_DATA)

%.ui.h: $(srcdir)/%.glade Makefile
	$(AM_V_GEN) xdt-csource --static --name=$(subst -,_,$(basename $(notdir $<)))_ui $< > $@

