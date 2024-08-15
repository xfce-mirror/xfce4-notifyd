panel_plugin_generated_sources = []

panel_plugin_generated_sources += gnome.compile_resources(
  'notification-plugin-settings-ui',
  'notification-plugin-settings-ui.gresource.xml',
  extra_args: [
    '--manual-register',
  ],
  export: false,
  install: false,
)

shared_module(
  'notification-plugin',
  [
    'notification-plugin.c',
    'notification-plugin-dialogs.c',
    'notification-plugin-log.c',
  ] + panel_plugin_generated_sources,
  gnu_symbol_visibility: 'hidden',
  include_directories: [
    include_directories('..'),
  ],
  c_args: [
    '-DG_LOG_DOMAIN="xfce4-notification-plugin"',
  ],
  dependencies: [
    gtk,
    libxfce4util,
    libxfce4ui,
    libxfce4panel,
    xfconf,
  ],
  link_with: [
    libxfce_notify_common,
  ],
  install: true,
  install_dir: get_option('prefix') / get_option('libdir') / 'xfce4' / 'panel' / 'plugins',
)

i18n.merge_file(
  output: 'notification-plugin.desktop',
  input: 'notification-plugin.desktop.in',
  po_dir: '../po',
  type: 'desktop',
  install: true,
  install_dir: get_option('prefix') / get_option('datadir') / 'xfce4' / 'panel' / 'plugins',
)
