if get_option('tests')
  test_programs = [
    'test-actions',
    'test-icons',
    'test-positioning',
    'test-text',
  ]

  foreach test_program : test_programs
    executable(
      test_program,
      [
        '@0@.c'.format(test_program),
      ],
      include_directories: [
        include_directories('..'),
      ],
      dependencies: [
        gtk,
        libnotify,
      ],
      install: false,
    )
  endforeach
endif
