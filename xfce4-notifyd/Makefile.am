
xfce4_notifyddir = $(HELPER_PATH_PREFIX)/xfce4/notifyd
xfce4_notifyd_PROGRAMS = \
	xfce4-notifyd

xfce4_notifyd_SOURCES = \
	$(top_srcdir)/common/xfce-notify-common.h \
	main.c \
	xfce-notify-fdo-gbus.c \
	xfce-notify-fdo-gbus.h \
	xfce-notify-gbus.c \
	xfce-notify-gbus.h \
	xfce-notify-log.c \
	xfce-notify-log.h \
	xfce-notify-daemon.c \
	xfce-notify-daemon.h \
	xfce-notify-daemon-log.c \
	xfce-notify-daemon-log.h \
	xfce-notify-marshal.c \
	xfce-notify-marshal.h \
	xfce-notify-window.c \
	xfce-notify-window.h

xfce4_notifyd_CFLAGS = \
	-DG_LOG_DOMAIN=\"xfce4-notifyd\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(builddir) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(GTK_CFLAGS) \
	$(SQLITE_CFLAGS) \
	$(LIBX11_CFLAGS) \
	$(CANBERRA_GTK_CFLAGS) \
	$(GTK_LAYER_SHELL_CFLAGS)

xfce4_notifyd_LDADD = \
	$(top_builddir)/common/libxfce-notifyd-common.la \
	$(LIBXFCE4UTIL_LIBS) \
	$(XFCONF_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(GTK_LIBS) \
	$(SQLITE_LIBS) \
	$(LIBX11_LIBS) \
	$(CANBERRA_GTK_LIBS) \
	$(GTK_LAYER_SHELL_LIBS)

service_in_files = \
	org.xfce.xfce4-notifyd.Notifications.service.in \
	org.xfce.xfce4-notifyd.Notifyd.service.in
systemd_user_in_files = xfce4-notifyd.service.in
autostart_in_in_files = xfce4-notifyd.desktop.in.in
autostart_in_files = xfce4-notifyd.desktop.in

dist_noinst_DATA = \
	$(service_in_files) \
	$(systemd_user_in_files) \
	$(autostart_in_in_files) \
	xfce-notify-dbus.xml \
	xfce-notify-fdo-dbus.xml \
	xfce-notify-marshal.list

if USE_DBUS_START_DAEMON
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)

systemd_userdir = $(prefix)/lib/systemd/user
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)

distclean_service = $(service_DATA) $(systemd_user_DATA)
else
autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

distclean_autostart = $(autostart_DATA) $(autostart_in_files)
endif

built_xfce4_notifyd = \
	xfce-notify-fdo-gbus.c \
	xfce-notify-fdo-gbus.h \
	xfce-notify-gbus.c \
	xfce-notify-gbus.h \
	xfce-notify-marshal.c \
	xfce-notify-marshal.h

BUILT_SOURCES = \
	$(built_xfce4_notifyd)

CLEANFILES = \
	$(built_xfce4_notifyd)

DISTCLEANFILES = \
	$(distclean_autostart) \
	$(distclean_autostart)

%.service: $(srcdir)/%.service.in Makefile
	$(AM_V_GEN) sed -e "s,\@notifydir\@,$(xfce4_notifyddir),g" < $< > $@

%.desktop.in: $(srcdir)/%.desktop.in.in Makefile
	$(AM_V_GEN) sed -e "s,\@notifydir\@,$(xfce4_notifyddir),g" < $< > $@

%.desktop: %.desktop.in
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

xfce-notify-fdo-gbus.h xfce-notify-fdo-gbus.c: $(srcdir)/xfce-notify-fdo-dbus.xml Makefile
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.Notifications \
		--c-namespace Xfce 	\
		--generate-c-code xfce-notify-fdo-gbus \
		--glib-max-allowed $(GLIB_REQUIRED_VERSION) \
		$<

xfce-notify-gbus.h xfce-notify-gbus.c: $(srcdir)/xfce-notify-dbus.xml Makefile
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.xfce.Notifyd \
		--c-namespace Xfce 	\
		--generate-c-code xfce-notify-gbus \
		--glib-max-allowed $(GLIB_REQUIRED_VERSION) \
		$<

xfce-notify-marshal.h: $(srcdir)/xfce-notify-marshal.list Makefile
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --quiet --prefix=xfce_notify_marshal --header $< > $@

xfce-notify-marshal.c: $(srcdir)/xfce-notify-marshal.list Makefile
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --quiet --prefix=xfce_notify_marshal --include-header xfce-notify-marshal.h --body $< > $@
