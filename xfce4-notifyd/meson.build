daemon_generated_sources = []

daemon_generated_sources += gnome.genmarshal(
  'xfce-notify-marshal',
  sources: 'xfce-notify-marshal.list',
  install_header: false,
  prefix: 'xfce_notify_marshal',
  extra_args: [
    '--quiet',
  ],
)

daemon_generated_sources += gnome.gdbus_codegen(
  'xfce-notify-fdo-gbus',
  sources: 'xfce-notify-fdo-dbus.xml',
  interface_prefix: 'org.freedesktop.Notifications',
  namespace: 'Xfce',
  install_header: false,
)

daemon_generated_sources += gnome.gdbus_codegen(
  'xfce-notify-gbus',
  sources: 'xfce-notify-dbus.xml',
  interface_prefix: 'org.xfce.Notifyd',
  namespace: 'Xfce',
  install_header: false,
)

daemon_install_dir = helper_path_prefix / 'xfce4' / 'notifyd'

executable(
  'xfce4-notifyd',
  [
    'main.c',
    'xfce-notification.c',
    'xfce-notify-log.c',
    'xfce-notify-daemon.c',
    'xfce-notify-daemon-log.c',
    'xfce-notify-types.c',
    'xfce-notify-window.c',
  ] + daemon_generated_sources,
  include_directories: [
    include_directories('..'),
  ],
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name()),
  ],
  dependencies: [
    gio,
    gio_unix,
    gtk,
    libxfce4util,
    libxfce4ui,
    xfconf,
    sqlite,
    libnotify,
    x11_deps,
    wayland_deps,
    sound_deps,
  ],
  link_with: [
    libxfce_notify_common,
  ],
  install: true,
  install_dir: daemon_install_dir,
)

systemd_service_dbus_text = ''
if systemd.found()
  systemd_service_dbus_text = 'SystemdService=xfce4-notifyd.service'
endif
daemon_service_data = configuration_data()
daemon_service_data.set('notifydir', daemon_install_dir)
daemon_service_data.set('systemd_service', systemd_service_dbus_text)


if get_option('dbus-start-daemon')
  dbus_service_dir = get_option('dbus-service-dir')
  if dbus_service_dir == ''
    dbus_service_dir = get_option('prefix') / get_option('datadir') / 'dbus-1' / 'services'
  endif

  configure_file(
    input: 'org.xfce.xfce4-notifyd.Notifications.service.in',
    output: 'org.xfce.xfce4-notifyd.Notifications.service',
    configuration: daemon_service_data,
    install_dir: dbus_service_dir,
  )

  configure_file(
    input: 'org.xfce.xfce4-notifyd.Notifyd.service.in',
    output: 'org.xfce.xfce4-notifyd.Notifyd.service',
    configuration: daemon_service_data,
    install_dir: dbus_service_dir,
  )
endif

if systemd.found()
  systemd_user_service_dir = get_option('systemd-user-service-dir')
  if systemd_user_service_dir == ''
    systemd_user_service_dir = get_option('prefix') / 'lib' / 'systemd' / 'user'
  endif

  configure_file(
    input: 'xfce4-notifyd.service.in',
    output: 'xfce4-notifyd.service',
    configuration: daemon_service_data,
    install_dir: systemd_user_service_dir,
  )
endif

if get_option('xdg-autostart')
  if systemd.found()
    xdg_autostart_exec = 'sh -c "systemctl --user start xfce4-notifyd.service 2>/dev/null || exec @0@/xfce4-notifyd"'.format(daemon_install_dir)
  else
    xdg_autostart_exec = '@0@/xfce4-notifyd'.format(daemon_install_dir)
  endif

  xdg_autostart_data = configuration_data()
  xdg_autostart_data.set('xdg_autostart_exec', xdg_autostart_exec)

  daemon_xdg_autostart_interpolated = configure_file(
    input: 'xfce4-notifyd.desktop.in',
    output: 'xfce4-notifyd.desktop.interpolated',
    configuration: xdg_autostart_data,
    install: false,
  )

  i18n.merge_file(
    output: 'xfce4-notifyd.desktop',
    input: daemon_xdg_autostart_interpolated, # 'xfce4-notifyd.desktop.interpolated',
    po_dir: '../po',
    type: 'desktop',
    install: true,
    install_dir: get_option('prefix') / get_option('sysconfdir') / 'xdg' / 'autostart',
  )
endif
