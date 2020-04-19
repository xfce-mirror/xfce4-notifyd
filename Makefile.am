AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = \
	icons	\
	common \
	panel-plugin \
	po

dist_autogen = autogen.sh
dist_misc = TODO
dist_intltool = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

distclean_intltool = \
	intltool-extract \
	intltool-merge \
	intltool-update

distuninstallcheck_listfiles = \
	find . -type f -print | grep -v ./share/icons/hicolor/icon-theme.cache

xfce4_notifyddir = $(HELPER_PATH_PREFIX)/xfce4/notifyd
xfce4_notifyd_PROGRAMS = xfce4-notifyd/xfce4-notifyd
bin_PROGRAMS = xfce4-notifyd-config/xfce4-notifyd-config

xfce4_notifyd_xfce4_notifyd_SOURCES = \
	xfce4-notifyd/main.c \
	xfce4-notifyd/xfce-notify-gbus.c \
	xfce4-notifyd/xfce-notify-gbus.h \
	xfce4-notifyd/xfce-notify-daemon.c \
	xfce4-notifyd/xfce-notify-daemon.h \
	xfce4-notifyd/xfce-notify-enum-types.c \
	xfce4-notifyd/xfce-notify-enum-types.h \
	xfce4-notifyd/xfce-notify-marshal.c \
	xfce4-notifyd/xfce-notify-marshal.h \
	xfce4-notifyd/xfce-notify-window.c \
	xfce4-notifyd/xfce-notify-window.h

xfce4_notifyd_config_xfce4_notifyd_config_SOURCES = \
	xfce4-notifyd-config/main.c \
	xfce4-notifyd-config/xfce4-notifyd-config.ui.h

common_cflags = \
	-I$(builddir)	\
	-DLOCALEDIR=\"$(localedir)\" \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(GTK3_CFLAGS)

xfce4_notifyd_xfce4_notifyd_CFLAGS = \
	-DG_LOG_DOMAIN=\"xfce4-notifyd\" \
	$(common_cflags) \
	$(LIBX11_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)


xfce4_notifyd_config_xfce4_notifyd_config_CFLAGS = \
	-DG_LOG_DOMAIN=\"xfce4-notifyd-config\" \
	$(LIBNOTIFY_CFLAGS) \
	$(common_cflags)

common_ldadd = \
	$(top_builddir)/common/libxfce-notifyd-common.la \
	$(LIBXFCE4UTIL_LIBS) \
	$(XFCONF_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(GTK3_LIBS)

xfce4_notifyd_xfce4_notifyd_LDADD = \
	$(common_ldadd) \
	$(LIBX11_LIBS)	\
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

xfce4_notifyd_config_xfce4_notifyd_config_LDADD = \
	$(common_ldadd) \
	$(LIBNOTIFY_LIBS)

service_in_files = xfce4-notifyd/org.xfce.xfce4-notifyd.Notifications.service.in
systemd_user_in_files = xfce4-notifyd/xfce4-notifyd.service.in
dist_service = \
	$(service_in_files) \
	$(systemd_user_in_files) \
	xfce4-notifyd/notify-dbus.xml \
	xfce4-notifyd/xfce-notify-marshal.list

autostart_in_files = xfce4-notifyd/xfce4-notifyd.desktop.in
dist_autostart = \
	$(autostart_in_files)

if USE_DBUS_START_DAEMON
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)

systemd_userdir = $(prefix)/lib/systemd/user
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)

distclean_service = $(service_DATA) $(systemd_user_DATA)
else
autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = $(autostart_in_files:.desktop.in=.desktop)

distclean_autostart = $(autostart_DATA)
endif

dist_man1_MANS = \
	xfce4-notifyd-config/xfce4-notifyd-config.1

desktopdir = $(datadir)/applications
desktop_in_files = xfce4-notifyd-config/xfce4-notifyd-config.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

icon_dir = $(datadir)/icons/hicolor
gtk_update_icon_cache = gtk-update-icon-cache -f -t $(icon_dir)

if HAVE_GTK_320
theme_dir = gtk-3.20
else
theme_dir = gtk-3.0
endif

brightthemedir = $(datadir)/themes/Bright/xfce-notify-4.0
dist_brighttheme_DATA = themes/$(theme_dir)/Bright/gtk.css

defaultthemedir = $(datadir)/themes/Default/xfce-notify-4.0
dist_defaulttheme_DATA = themes/$(theme_dir)/Default/gtk.css

retrothemedir = $(datadir)/themes/Retro/xfce-notify-4.0
dist_retrotheme_DATA = themes/$(theme_dir)/Retro/gtk.css

smokethemedir = $(datadir)/themes/Smoke/xfce-notify-4.0
dist_smoketheme_DATA = themes/$(theme_dir)/Smoke/gtk.css

zomgponiesthemedir = $(datadir)/themes/ZOMG-PONIES!/xfce-notify-4.0
dist_zomgponiestheme_DATA = themes/$(theme_dir)/ZOMG-PONIES!/gtk.css

dist_themes = \
	themes/gtk-3.0/Bright/gtk.css \
	themes/gtk-3.0/Default/gtk.css \
	themes/gtk-3.0/Retro/gtk.css \
	themes/gtk-3.0/Smoke/gtk.css \
	themes/gtk-3.0/ZOMG-PONIES!/gtk.css \
	themes/gtk-3.20/Bright/gtk.css \
	themes/gtk-3.20/Default/gtk.css \
	themes/gtk-3.20/Retro/gtk.css \
	themes/gtk-3.20/Smoke/gtk.css \
	themes/gtk-3.20/ZOMG-PONIES!/gtk.css

built_xfce4_notifyd = \
	xfce4-notifyd/xfce-notify-gbus.c \
	xfce4-notifyd/xfce-notify-gbus.h

clean_xfce4_notifyd = \
	$(built_xfce4_notifyd)

if MAINTAINER_MODE

built_xfce4_notifyd += \
	xfce4-notifyd/xfce-notify-marshal.c \
	xfce4-notifyd/xfce-notify-marshal.h \
	xfce4-notifyd/xfce-notify-enum-types.c \
	xfce4-notifyd/xfce-notify-enum-types.h

built_xfce4_notifyd_config = \
	xfce4-notifyd-config/xfce4-notifyd-config.ui.h

clean_xfce4_notifyd += \
	xfce4-notifyd/stamp-xfce-notify-enum-types.h \
	xfce4-notifyd/stamp-xfce-notify-marshal.h

endif

clean_xfce4_notifyd_config = \
	$(built_xfce4_notifyd_config) \
	$(desktop_DATA)

dist_xfce4_notifyd_config = \
	xfce4-notifyd-config/xfce4-notifyd-config.glade \
	$(desktop_in_files)

dist_noinst_DATA = \
	$(dist_misc) \
	$(dist_intltool) \
	$(dist_service) \
	$(dist_autostart) \
	$(dist_xfce4_notifyd_config) \
	$(dist_themes)

dist_noinst_SCRIPTS = \
	$(dist_autogen)

BUILT_SOURCES = \
	$(built_xfce4_notifyd) \
	$(built_xfce4_notifyd_config)

CLEANFILES = \
	$(clean_xfce4_notifyd) \
	$(clean_xfce4_notifyd_config)

DISTCLEANFILES = \
	$(distclean_intltool) \
	$(distclean_service) \
	$(distclean_autostart)

xfce4-notifyd/xfce-notify-gbus.h:
xfce4-notifyd/xfce-notify-gbus.c: $(srcdir)/xfce4-notifyd/notify-dbus.xml Makefile
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop.Notifications \
	--c-namespace XfceNotify 	\
	--generate-c-code xfce4-notifyd/xfce-notify-gbus \
	$(srcdir)/xfce4-notifyd/notify-dbus.xml

if MAINTAINER_MODE

xfce4-notifyd/xfce-notify-marshal.h: xfce4-notifyd/stamp-xfce-notify-marshal.h
	@true

xfce4-notifyd/stamp-xfce-notify-marshal.h: $(srcdir)/xfce4-notifyd/xfce-notify-marshal.list Makefile
	$(AM_V_GEN) ( \
	echo '/* this file is autogenerated -- do not edit */' > xfce4-notifyd/xfce-notify-marshal.h \
  && glib-genmarshal --prefix=xfce_notify_marshal --header $(srcdir)/xfce4-notifyd/xfce-notify-marshal.list \
  >> xfce4-notifyd/xfce-notify-marshal.h \
	&& echo timestamp >$@ )

xfce4-notifyd/xfce-notify-marshal.c: $(srcdir)/xfce4-notifyd/xfce-notify-marshal.list Makefile
	$(AM_V_GEN) ( \
	echo '/* this file is autogenerated -- do not edit */' >$@ \
	&& echo '#include "xfce-notify-marshal.h"' >$@ \
	&& glib-genmarshal --prefix=xfce_notify_marshal --body $(srcdir)/xfce4-notifyd/xfce-notify-marshal.list >>$@ \
	)

xfce4-notifyd/xfce-notify-enum-types.h: $(srcdir)/xfce4-notifyd/stamp-xfce-notify-enum-types.h
	@true

xfce4-notifyd/stamp-xfce-notify-enum-types.h: $(srcdir)/xfce4-notifyd/xfce-notify-window.h Makefile
	$(AM_V_GEN) ( \
	( cd $(srcdir) && glib-mkenums \
	        --fhead "#ifndef __XFCE_NOTIFY_ENUM_TYPES_H__\n#define __XFCE_NOTIFY_ENUM_TYPES_H__\n#include <glib-object.h>\nG_BEGIN_DECLS\n" \
	        --fprod "/* enumerations from \"@filename@\" */\n" \
	        --vhead "GType @enum_name@_get_type(void) G_GNUC_CONST;\n#define XFCE_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
	        --ftail "G_END_DECLS\n\n#endif /* __XFCE_NOTIFY_ENUM_TYPES_H__ */" \
	        $(srcdir)/xfce4-notifyd/xfce-notify-window.h ) >> xfce4-notifyd/xgen-xneth \
	&& ( cmp -s xfce4-notifyd/xgen-xneth xfce4-notifyd/xfce-notify-enum-types.h || cp xfce4-notifyd/xgen-xneth xfce4-notifyd/xfce-notify-enum-types.h ) \
	&& rm -f xfce4-notifyd/xgen-xneth \
	&& echo timestamp >$@ \
	)

xfce4-notifyd/xfce-notify-enum-types.c: $(srcdir)/xfce4-notifyd/xfce-notify-window.h Makefile
	$(AM_V_GEN) ( \
	( cd $(srcdir) && glib-mkenums \
	        --fhead "#include \"xfce-notify-enum-types.h\"\n#include \"xfce4-notifyd/xfce-notify-window.h\"\n" \
	        --fprod "\n/* enumerations from \"@filename@\" */" \
	        --vhead "GType\n@enum_name@_get_type(void)\n{\n\tstatic GType type = 0;\n\tif(type == 0) {\n\tstatic const G@Type@Value values[] = {"\
	        --vprod "\t{ @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	        --vtail "\t{ 0, NULL, NULL }\n\t};\n\ttype = g_@type@_register_static(\"@EnumName@\", values);\n  }\n\treturn type;\n}\n" \
	        $(srcdir)/xfce4-notifyd/xfce-notify-window.h ) >> xfce4-notifyd/xgen-xnetc \
	&& cp xfce4-notifyd/xgen-xnetc xfce4-notifyd/xfce-notify-enum-types.c \
	&& rm -f xfce4-notifyd/xgen-xnetc \
	)

xfce4-notifyd-config/xfce4-notifyd-config.ui.h: $(srcdir)/xfce4-notifyd-config/xfce4-notifyd-config.glade
	$(AM_V_GEN) ( \
	xdt-csource --static --name=xfce4_notifyd_config_ui $< >$@ \
	)

endif

install-data-hook:
	@-if test -z "$(DESTDIR)"; then \
	    echo "Updating Gtk icon cache."; \
	    $(gtk_update_icon_cache); \
	else \
	    echo "*** Icon cache not updated.  Remember to run:"; \
	    echo "***"; \
	    echo "***   $(gtk_update_icon_cache)"; \
	    echo "***"; \
	fi

xfce4-notifyd/%.service: $(srcdir)/xfce4-notifyd/%.service.in Makefile
	sed -e "s,\@notifydir\@,$(xfce4_notifyddir),g" < $< > $@

xfce4-notifyd/%.desktop: $(srcdir)/xfce4-notifyd/%.desktop.in Makefile
	sed -e "s,\@notifydir\@,$(xfce4_notifyddir),g" < $< > $@

distclean-local:
	-rm -rf *.cache *~

checksums: distcheck
	sha1sum $(PACKAGE)-$(VERSION).tar.bz2 > $(PACKAGE)-$(VERSION).tar.bz2.sha1.asc

gpg-sign: checksums
	gpg -b --armor $(PACKAGE)-$(VERSION).tar.bz2

release: gpg-sign
	-mkdir releases/
	mv $(PACKAGE)-$(VERSION).tar.bz2* releases/
	-rm $(PACKAGE)-$(VERSION).tar.gz

.PHONY: ChangeLog

ChangeLog: Makefile
	(GIT_DIR=$(top_srcdir)/.git git log > .changelog.tmp \
	&& mv .changelog.tmp ChangeLog; rm -f .changelog.tmp) \
	|| (touch ChangeLog; echo 'Git directory not found: installing possibly empty changelog.' >&2)

dist-hook: ChangeLog

#Tests
noinst_PROGRAMS = \
	tests/test-text \
	tests/test-positioning \
	tests/test-actions \
	tests/test-icons

tests_cflags = \
	-I$(top_srcdir) \
	$(LIBNOTIFY_CFLAGS)

tests_ldadd = \
	$(LIBNOTIFY_LIBS)

icon_tests_cflags = \
	$(common_cflags)  \
	$(tests_cflags)

icon_tests_ldadd = \
	$(common_ldadd)   \
	$(tests_ldadd)

tests_test_actions_SOURCES = tests/test-actions.c
tests_test_actions_CFLAGS = $(tests_cflags)
tests_test_actions_LDADD = $(tests_ldadd)

tests_test_positioning_SOURCES = tests/test-positioning.c
tests_test_positioning_CFLAGS = $(tests_cflags)
tests_test_positioning_LDADD = $(tests_ldadd)

tests_test_text_SOURCES = tests/test-text.c
tests_test_text_CFLAGS = $(tests_cflags)
tests_test_text_LDADD = $(tests_ldadd)

tests_test_icons_SOURCES = tests/test-icons.c
tests_test_icons_CFLAGS = $(icon_tests_cflags)
tests_test_icons_LDADD = $(icon_tests_ldadd)
