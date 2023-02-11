dnl configure.ac
dnl
dnl xfce4-notifyd - server-side implementation of the fd.o notifications spec
dnl
dnl Copyright (c) 2008 Brian Tarricone <brian@tarricone.org>
dnl Copyright (c) 2019 Simon Steinbeiss <simon@xfce.org>
dnl

m4_define([notifications_spec_version], [1.2])
m4_define([glib_minimum_version], [2.68])
m4_define([gtk_minimum_version], [3.22])
m4_define([canberra_minimum_version], [0.30])
m4_define([sqlite_minimum_version], [3.34])

dnl version info
XDT_VERSION_INIT([0.8.0], [git])

dnl init autoconf
AC_INIT([xfce4-notifyd], [xdt_version],
        [https://gitlab.xfce.org/apps/xfce4-notifyd],
        [xfce4-notifyd])
AC_PREREQ([2.59c])
AC_CONFIG_SRCDIR([xfce4-notifyd/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl init automake
AM_INIT_AUTOMAKE([1.11 dist-bzip2 tar-ustar no-dist-gzip subdir-objects foreign])
AM_MAINTAINER_MODE()

dnl set helper path prefix
AC_ARG_WITH([helper-path-prefix],
            [AS_HELP_STRING([--with-helper-path-prefix=PATH],[Path prefix under which helper executables will be installed (default: $libdir)])],
            [HELPER_PATH_PREFIX="$withval"],
            [HELPER_PATH_PREFIX="$libdir"])
AC_SUBST([HELPER_PATH_PREFIX])

dnl check for UNIX variants
AC_USE_SYSTEM_EXTENSIONS()

dnl check for basic programs
AC_PROG_CC()
AM_PROG_CC_C_O()
LT_PATH_LD([])
AC_PROG_INSTALL()
IT_PROG_INTLTOOL()
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

dnl check for standard header files
AC_CHECK_HEADERS([errno.h fcntl.h  grp.h signal.h stdlib.h string.h \
                  sys/stat.h sys/time.h sys/types.h sys/wait.h \
                  unistd.h math.h])

dnl Check for i18n support
XDT_I18N()

dnl check for x11
XDT_CHECK_LIBX11_REQUIRE()

dnl check for required packages
XDT_CHECK_PACKAGE([GIO], [gio-2.0], [glib_minimum_version])
XDT_CHECK_PACKAGE([GIO_UNIX], [gio-unix-2.0], [glib_minimum_version])
XDT_CHECK_PACKAGE([GLIB], [glib-2.0], [glib_minimum_version])
XDT_CHECK_PACKAGE([GTK], [gtk+-3.0], [gtk_minimum_version])
XDT_CHECK_PACKAGE([LIBNOTIFY], [libnotify], [0.7.0])
XDT_CHECK_PACKAGE([LIBXFCE4PANEL], [libxfce4panel-2.0], [4.12.0])
XDT_CHECK_PACKAGE([LIBXFCE4UI], [libxfce4ui-2], [4.12.0])
XDT_CHECK_PACKAGE([LIBXFCE4UTIL], [libxfce4util-1.0], [4.12.0])
XDT_CHECK_PACKAGE([XFCONF], [libxfconf-0], [4.10.0])
XDT_CHECK_PACKAGE([SQLITE], [sqlite3], [sqlite_minimum_version])

XDT_CHECK_OPTIONAL_PACKAGE([GDK_X11], [gdk-x11-3.0], [gtk_minimum_version], [gdk-x11], [X11 backend for GDK])
XDT_CHECK_OPTIONAL_PACKAGE([GDK_WAYLAND], [gdk-wayland-3.0], [gtk_minimum_version], [gdk-wayland], [Wayland backend for GDK])
XDT_CHECK_OPTIONAL_PACKAGE([GTK_LAYER_SHELL], [gtk-layer-shell-0], [0.7.0], [gtk-layer-shell], [gtk-layer-shell Wayland library])

if test x"$GDK_X11_FOUND" = x"yes"; then
    enable_x11=yes
    AC_DEFINE([ENABLE_X11], [1], [Set if X11 is enabled])
else
    enable_x11=no
fi

if test x"$GDK_WAYLAND_FOUND" = x"yes" -a x"$GTK_LAYER_SHELL_FOUND" = x"yes"; then
    enable_wayland=yes
    AC_DEFINE([ENABLE_WAYLAND], [1], [Set if Wayland is enabled])
else
    enable_wayland=no
fi

AC_ARG_ENABLE([sound],
              [AS_HELP_STRING([--enable-sound],
                              [Whether or not to enable sound support (default=auto)])],
              [enable_sound=$enableval],
              [enable_sound=auto])
XDT_CHECK_OPTIONAL_PACKAGE([CANBERRA_GTK], [libcanberra-gtk3], [canberra_minimum_version], [canberra], [Canberra sound theme library])

if test x"$enable_sound" != x"no"; then
    if test x"$CANBERRA_GTK_FOUND" = x"yes"; then
        enable_sound=yes
        AC_DEFINE([ENABLE_SOUND], [1], [Set if sound is enabled])
    elif test x"$enable_sound" = x"yes"; then
        AC_MSG_ERROR([Sound support was requested, but dependencies were not met])
        exit 1
    fi
else
    enable_sound=no
fi

dnl ***********************************
dnl *** Check for gdbus-codegen ***
dnl ***********************************
AC_ARG_VAR([GDBUS_CODEGEN], [D-Bus code and documentation generator from XML D-Bus interface definitions])
AC_PATH_PROG([GDBUS_CODEGEN], [gdbus-codegen], [no])
if test x"$GDBUS_CODEGEN" = x"no"; then
  AC_MSG_ERROR([could not find gdbus-codegen in \$PATH. You can run
  ./configure GDBUS_CODEGEN=/path/to/gdbus-codegen to define
  a custom location for it.])
fi

dnl legacy support
AC_ARG_ENABLE([old-notification-closed-signature],
              [AS_HELP_STRING([--enable-old-notification-closed-signature],
                              [Use the old NotificationClosed signal signature that leaves out the 'reason' parameter.  You want to enable this if your system has libnotify 0.4.4 or earlier.])],
              [use_old_nc_sig=$enableval], [use_old_nc_sig=no])
AS_IF([test "x$use_old_nc_sig" = "xyes"],
      [AC_DEFINE([USE_OLD_NOTIFICATION_CHANGED_SIGNATURE], [1],
                 [Set if we should use the old NotificationClosed signal signature])])

dnl legacy support
AC_ARG_ENABLE([old-get-server-information-signature],
              [AS_HELP_STRING([--enable-old-get-server-information-signature],
                              [Use the old GetServerInformation method signature that leaves out the 'spec_version' parameter. (default=no)])],
              [use_old_gsi_sig=$enableval], [use_old_gsi_sig=no])
AS_IF([test "x$use_old_gsi_sig" = "xyes"],
      [AC_DEFINE([USE_OLD_GET_SERVER_INFORMATION_SIGNATURE], [1],
                 [Set if we should use the old GetServerInformation method signature])])
AM_CONDITIONAL([USE_OLD_GET_SERVER_INFORMATION_SIGNATURE],
               [test "x$use_old_gsi_sig" = "xyes"])

dnl dbus start daemon
AC_ARG_ENABLE([dbus-start-daemon],
              [AS_HELP_STRING([--enable-dbus-start-daemon],
                              [Start daemon via DBus instead of regular session autostart. (default=no)])],
              [use_dbus_start_daemon=$enableval], [use_dbus_start_daemon=no])
AM_CONDITIONAL([USE_DBUS_START_DAEMON],
               [test "x$use_dbus_start_daemon" = "xyes"])

dnl create #define for spec version
AC_DEFINE_UNQUOTED([NOTIFICATIONS_SPEC_VERSION], ["notifications_spec_version"],
                   [Set to the version of the freedesktop.org notifications spec we support])

dnl check for debugging support
XDT_FEATURE_DEBUG([xdt_debug_default])

AC_CONFIG_FILES(
[
    Makefile
    icons/Makefile
    icons/16x16/Makefile
    icons/48x48/Makefile
    icons/128x128/Makefile
    icons/scalable/Makefile
    icons/symbolic/Makefile
    common/Makefile
    panel-plugin/Makefile
    po/Makefile.in
])
AC_OUTPUT

dnl ***************************
dnl *** Print configuration ***
dnl ***************************
echo
echo "Build Configuration:"
echo
echo "* Installation prefix:       $libdir"
echo "* Debug Support:             $enable_debug"
echo "* X11 Support:               $enable_x11"
echo "* Wayland Support:           $enable_wayland"
echo "* Sound support:             $enable_sound"
if test "x$use_dbus_start_daemon" = "xyes"; then
echo "* Startup method:            DBus"
else
echo "* Startup method:            Autostart"
fi
echo
