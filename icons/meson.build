app_icon_png_sizes = [16, 48, 128]
foreach size : app_icon_png_sizes
  install_data(
    '@0@x@0@/org.xfce.notification.png'.format(size),
    install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / '@0@x@0@'.format(size) / 'apps',
  )
endforeach

install_data(
  'scalable/org.xfce.notification.svg',
  install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

install_data(
  [
    'symbolic/notification-disabled-symbolic.svg',
    'symbolic/notification-symbolic.svg',
    'symbolic/org.xfce.notification.unread-emblem-symbolic.svg',
  ],
  install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'status',
)
