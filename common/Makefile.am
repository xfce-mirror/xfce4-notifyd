AM_CPPFLAGS = \
	-I${top_srcdir}

noinst_LTLIBRARIES = \
	libxfce-notifyd-common.la

libxfce_notifyd_common_la_SOURCES = \
	xfce-notify-common.c \
	xfce-notify-common.h \
	xfce-notify-log.c \
	xfce-notify-log.h \
	xfce-notify-log-util.c \
	xfce-notify-log-util.h

libxfce_notifyd_common_la_CFLAGS = \
	$(LIBXFCE4UI_CFLAGS) \
	$(GTK3_CFLAGS) \
	$(SQLITE_CFLAGS) \
	$(PLATFORM_CFLAGS)

libxfce_notifyd_common_la_LIBADD = \
	$(LIBXFCE4UI_LIBS) \
	$(GTK3_LIBS) \
	$(SQLITE_LIBS)

libxfce_notifyd_common_la_LDFLAGS = \
	-no-undefined \
	$(PLATFORM_LDFLAGS)
