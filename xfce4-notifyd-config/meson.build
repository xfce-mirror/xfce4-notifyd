config_generated_sources = []

config_generated_sources = gnome.compile_resources(
  'xfce-notify-config-ui',
  'xfce-notify-config-ui.gresource.xml',
  extra_args: [
    '--manual-register',
  ],
  export: false,
  install: false,
)

executable(
  'xfce4-notifyd-config',
  [
    'main.c',
    'xfce-notify-log-viewer.c',
    xfce_revision_h,
  ] + config_generated_sources,
  include_directories: [
    include_directories('..'),
  ],
  c_args: [
    '-DG_LOG_DOMAIN="xfce4-notifyd-config"',
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
  ],
  link_with: [
    libxfce_notify_common,
  ],
  install: true,
  install_dir: get_option('prefix') / get_option('bindir'),
)

install_man('xfce4-notifyd-config.1')

i18n.merge_file(
  output: 'xfce4-notifyd-config.desktop',
  input: 'xfce4-notifyd-config.desktop.in',
  po_dir: '../po',
  type: 'desktop',
  install: true,
  install_dir: get_option('prefix') / get_option('datadir') / 'applications',
)
