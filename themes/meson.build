themes = {
  'Bright': ['gtk.css'],
  'Default': ['gtk.css'],
  'Retro': ['gtk.css'],
  'Smoke': ['gtk.css'],
  'XP-Balloon': [
    'gtk.css',
    'msgbox-bottom-left.png',
    'msgbox-bottom-right.png',
    'msgbox-top-left.png',
    'msgbox-top-right.png',
  ],
  'ZOMG-PONIES!': ['gtk.css'],
}

foreach theme, theme_files : themes
  theme_paths = []
  foreach theme_file : theme_files
    theme_paths += 'gtk-3.20' / theme / theme_file
  endforeach

  install_data(
    theme_paths,
    install_dir: get_option('prefix') / get_option('datadir') / 'themes' / theme / 'xfce-notify-4.0',
  )
endforeach
