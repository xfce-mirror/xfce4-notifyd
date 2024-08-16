project(
  'xfce4-notifyd',
  'c',
  version : '0.9.6-dev',
  license : 'GPL-2',
  meson_version : '>= 0.58.0',
  default_options : ['c_std=gnu99', 'buildtype=debugoptimized', 'warning_level=2', 'b_ndebug=if-release']
)

project_namespace = 'apps'
pkgdatadir = get_option('prefix') / get_option('datadir') / meson.project_name()

cc = meson.get_compiler('c')
pkgconfig = import('pkgconfig')
gnome = import('gnome')
i18n = import('i18n')

dependency_versions = {
  'canberra': '>= 0.30',
  'glib': '>= 2.68.0',
  'gtk': '>= 3.22.0',
  'gtk-layer-shell': '>= 0.7.0',
  'libnotify': '>= 0.7.0',
  'libx11': '>= 1.6.7',
  'sqlite': '>= 3.34',
  'systemd': '>= 245',
  'xfce4': '>= 4.18.0',
}

glib = dependency('glib-2.0', version: dependency_versions['glib'])
gio = dependency('gio-2.0', version: dependency_versions['glib'])
gio_unix = dependency('gio-unix-2.0', version: dependency_versions['glib'])
gtk = dependency('gtk+-3.0', version: dependency_versions['gtk'])
libxfce4util = dependency('libxfce4util-1.0', version: dependency_versions['xfce4'])
libxfce4ui = dependency('libxfce4ui-2', version: dependency_versions['xfce4'])
libxfce4panel = dependency('libxfce4panel-2.0', version: dependency_versions['xfce4'])
xfconf = dependency('libxfconf-0', version: dependency_versions['xfce4'])
libxfce4panel = dependency('libxfce4panel-2.0', version: dependency_versions['xfce4'])
libnotify = dependency('libnotify', version: dependency_versions['libnotify'])
sqlite = dependency('sqlite3', version: dependency_versions['sqlite'])

libx11 = dependency('x11', version: dependency_versions['libx11'], required: get_option('x11'))
gdk_x11 = dependency('gdk-x11-3.0', version: dependency_versions['gtk'], required: get_option('x11'))

gdk_wayland = dependency('gdk-wayland-3.0', version: dependency_versions['gtk'], required: get_option('wayland'))
gtk_layer_shell = dependency('gtk-layer-shell-0', version: dependency_versions['gtk-layer-shell'], required: get_option('wayland'))

canberra_gtk3 = dependency('libcanberra-gtk3', version: dependency_versions['canberra'], required: get_option('sound'))

systemd = dependency('systemd', version: dependency_versions['systemd'], required: get_option('systemd'))

feature_cflags = []

enable_x11 = libx11.found() and gdk_x11.found()
x11_deps = []
if enable_x11
  x11_deps += [libx11, gdk_x11]
  feature_cflags += '-DENABLE_X11=1'
endif

enable_wayland = gdk_wayland.found() and gtk_layer_shell.found()
wayland_deps = []
if enable_wayland
  wayland_deps += [gdk_wayland, gtk_layer_shell]
  feature_cflags += '-DENABLE_WAYLAND=1'
endif

enable_sound = canberra_gtk3.found()
sound_deps = []
if enable_sound
  sound_deps += canberra_gtk3
  feature_cflags += '-DENABLE_SOUND=1'
endif

extra_cflags = []
extra_cflags_check = [
  '-Wmissing-declarations',
  '-Wmissing-noreturn',
  '-Wold-style-definition',
  '-Wredundant-decls',
  '-Wpointer-arith',
  '-Winit-self',
  '-Wshadow',
  '-Wmissing-include-dirs',
  '-Wundef',
  '-Wformat',
  '-Wformat-security',
  '-Wformat-y2k',
  '-Wnested-externs',
  '-Wno-unused-parameter',
  '-Wno-declaration-after-statement',
  '-Werror=implicit-function-declaration',
]

buildtype = get_option('buildtype')
if buildtype == 'debug' or buildtype == 'debugoptimized'
  extra_cflags_check += '-fstack-protector-strong'
  extra_cflags += '-DDEBUG=1'

  if buildtype == 'debug'
    extra_cflags += [
      '-DG_ENABLE_DEBUG',
      '-DDEBUG_TRACE=1',
    ]
  endif
elif buildtype == 'minsize'
  extra_cflags += [
    '-DNDEBUG',
    '-DG_DISABLE_CAST_CHECKS',
    '-DG_DISABLE_ASSERT',
  ]
endif

if dependency_versions.has_key('glib')
  glib_version_parts = dependency_versions['glib'].split(' ')
  glib_min_version_parts = glib_version_parts[1].split('.')
  glib_min_version_define = 'GLIB_VERSION_@0@_@1@'.format(glib_min_version_parts[0], glib_min_version_parts[1])
  extra_cflags += [
    '-DGLIB_VERSION_MIN_REQUIRED=@0@'.format(glib_min_version_define),
    '-DGLIB_VERSION_MAX_ALLOWED=@0@'.format(glib_min_version_define),
    '-DG_LOG_USE_STRUCTURED=1',
  ]
endif

version_parts = meson.project_version().split('git')[0].split('.')

extra_cflags += [
  '-DPACKAGE="@0@"'.format(meson.project_name()),
  '-DPACKAGE_NAME="@0@"'.format(meson.project_name()),
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  '-DVERSION="@0@"'.format(meson.project_version()),
  '-DPACKAGE_STRING="@0@ @1@"'.format(meson.project_name(), meson.project_version()),
  '-DPACKAGE_DATADIR="@0@"'.format(pkgdatadir),
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  '-DPACKAGE_BUGREPORT="https://gitlab.xfce.org/@0@/@1@/-/issues"'.format(project_namespace, meson.project_name()),
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
  '-DSHORT_VERSION="@0@.@1@"'.format(version_parts[0], version_parts[1]),
  '-DPREFIX="@0@"'.format(get_option('prefix')),
  '-DBINDIR="@0@"'.format(get_option('prefix') / get_option('bindir')),
  '-DDATADIR="@0@"'.format(get_option('prefix') / get_option('datadir')),
  '-DINCLUDEDIR="@0@"'.format(get_option('prefix') / get_option('includedir')),
  '-DLIBDIR="@0@"'.format(get_option('prefix') / get_option('libdir')),
  '-DLIBEXECDIR="@0@"'.format(get_option('prefix') / get_option('libexecdir')),
  '-DLOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  '-DLOCALSTATEDIR="@0@"'.format(get_option('prefix') / get_option('localstatedir')),
  '-DSBINDIR="@0@"'.format(get_option('prefix') / get_option('sbindir')),
  '-DSYSCONFDIR="@0@"'.format(get_option('prefix') / get_option('sysconfdir')),
  '-DHAVE_XFCE_REVISION_H=1',
]

headers = [
  'math.h',
  'stdlib.h',
  'string.h',
]
foreach header : headers
  if cc.check_header(header)
    feature_cflags += '-DHAVE_@0@=1'.format(header.replace('.', '_').to_upper())
  endif
endforeach

add_project_arguments(cc.get_supported_arguments(extra_cflags_check), language: 'c')
add_project_arguments(extra_cflags, language: 'c')
add_project_arguments(feature_cflags, language: 'c')

xfce_revision_h = vcs_tag(
  command: ['git', 'rev-parse', '--short', 'HEAD'],
  fallback: 'UNKNOWN',
  input: 'xfce-revision.h.in',
  output: 'xfce-revision.h',
  replace_string: '@REVISION@',
)

helper_path_prefix = get_option('helper-path-prefix')
if helper_path_prefix == ''
  helper_path_prefix = get_option('prefix') / get_option('libdir')
endif

subdir('common')
subdir('xfce4-notifyd')
subdir('xfce4-notifyd-config')
subdir('panel-plugin')
subdir('icons')
subdir('themes')
subdir('po')
subdir('tests')
