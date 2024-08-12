common_generated_sources = []

common_generated_sources += gnome.gdbus_codegen(
  'xfce-notify-log-gbus',
  sources: 'xfce-notify-log-dbus.xml',
  interface_prefix: 'org.xfce.Notifyd.Log',
  namespace: 'Xfce',
  install_header: false,
)

common_generated_sources += gnome.mkenums_simple(
  'xfce-notify-enum-types',
  sources: [
    'xfce-notify-common.h',
  ]
)

libxfce_notify_common = static_library(
  'libxfce-notifyd-common',
  [
	'xfce-notify-common.c',
	'xfce-notify-log-types.c',
	'xfce-notify-log-util.c',
  ] + common_generated_sources,
  gnu_symbol_visibility: 'hidden',
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name()),
  ],
  include_directories: [
    include_directories('..'),
  ],
  dependencies: [
    gio_unix,
    xfconf,
    libxfce4ui,
    gtk,
    sqlite,
  ],
  install: false,
)
