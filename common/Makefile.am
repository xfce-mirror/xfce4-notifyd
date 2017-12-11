AM_CPPFLAGS = \
	-I${top_srcdir}

noinst_LTLIBRARIES = \
	libxfce-notifyd-common.la

libxfce_notifyd_common_la_SOURCES = \
	xfce-notify-log.c \
	xfce-notify-log.h

libxfce_notifyd_common_la_CFLAGS = \
	$(LIBXFCE4UI_CFLAGS) \
	$(GTK3_CFLAGS) \
	$(PLATFORM_CFLAGS)

libxfce_notifyd_common_la_LIBADD = \
	$(LIBXFCE4UI_LIBS) \
	$(GTK3_LIBS)

libxfce_notifyd_common_la_LDFLAGS = \
	-no-undefined \
	$(PLATFORM_LDFLAGS)
