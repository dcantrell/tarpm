pandoc = find_program('pandoc', required: get_option('docs'))

if get_option('docs')
    tarpm_man = custom_target(
        'tarpm_1',
        output : 'tarpm.1',
        command : [ pandoc, '-f', 'markdown', '-t', 'man', '-o', '@OUTPUT@', meson.current_source_dir() / 'tarpm.md' ],
        install : true,
        install_dir : get_option('mandir') / 'man1'
    )
endif
