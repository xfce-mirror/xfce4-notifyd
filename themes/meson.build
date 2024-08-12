themes = [
  'Bright',
  'Default',
  'Retro',
  'Smoke',
  'ZOMG-PONIES!',
]

foreach theme : themes
  install_data(
    [
      'gtk-3.20/@0@/gtk.css'.format(theme),
    ],
    install_dir: get_option('prefix') / get_option('datadir') / 'themes' / theme / 'xfce-notify-4.0',
  )
endforeach
