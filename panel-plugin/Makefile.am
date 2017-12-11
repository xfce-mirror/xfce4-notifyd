AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"xfce4-notification-plugin\" \
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\" \
	$(PLATFORM_CPPFLAGS)

#
# Notification plugin
#
plugin_LTLIBRARIES = \
	libnotification-plugin.la

plugindir = \
	$(libdir)/xfce4/panel/plugins

libnotification_plugin_la_SOURCES = \
	notification-plugin.c \
	notification-plugin.h \
	notification-plugin-dialogs.c \
	notification-plugin-dialogs.h \
	notification-plugin-log.c \
	notification-plugin-log.h

libnotification_plugin_la_CFLAGS = \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(LIBXFCE4PANEL_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(PLATFORM_CFLAGS)

libnotification_plugin_la_LDFLAGS = \
       -avoid-version \
       -module \
       -no-undefined \
       -export-symbols-regex '^xfce_panel_module_(preinit|init|construct)' \
       $(PLATFORM_LDFLAGS)

libnotification_plugin_la_LIBADD = \
	$(top_builddir)/common/libxfce-notifyd-common.la \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(LIBXFCE4PANEL_LIBS)	\
	$(XFCONF_LIBS)

#
# Desktop file
#
desktopdir =								\
	$(datadir)/xfce4/panel/plugins

desktop_DATA =								\
	notification-plugin.desktop

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =								\
	notification-plugin.desktop.in

CLEANFILES =								\
	$(desktop_DATA)

# vi:set ts=8 sw=8 noet ai nocindent syntax=automake:
