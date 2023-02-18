AM_CPPFLAGS = \
	-I${top_srcdir}

noinst_LTLIBRARIES = \
	libxfce-notifyd-common.la

libxfce_notifyd_common_la_SOURCES = \
	xfce-notify-common.c \
	xfce-notify-common.h \
	xfce-notify-log-types.h \
	xfce-notify-log-types.c \
	xfce-notify-log-gbus.c \
	xfce-notify-log-gbus.h \
	xfce-notify-log-util.c \
	xfce-notify-log-util.h

libxfce_notifyd_common_la_CFLAGS = \
	$(LIBXFCE4UI_CFLAGS) \
	$(GTK3_CFLAGS) \
	$(SQLITE_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(PLATFORM_CFLAGS)

libxfce_notifyd_common_la_LIBADD = \
	$(LIBXFCE4UI_LIBS) \
	$(GTK3_LIBS) \
	$(XFCONF_LIBS) \
	$(SQLITE_LIBS)

libxfce_notifyd_common_la_LDFLAGS = \
	-no-undefined \
	$(PLATFORM_LDFLAGS)

xfce-notify-log-gbus.h xfce-notify-log-gbus.c: $(srcdir)/xfce-notify-log-dbus.xml Makefile
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.xfce.Notifyd.Log \
		--c-namespace Xfce \
		--generate-c-code xfce-notify-log-gbus \
		--glib-max-allowed $(GLIB_REQUIRED_VERSION) \
		$<

dist_noinst_DATA = \
	xfce-notify-log-dbus.xml

BUILT_SOURCES = \
	xfce-notify-log-gbus.c \
	xfce-notify-log-gbus.h

CLEANFILES = \
	xfce-notify-log-gbus.c \
	xfce-notify-log-gbus.h
