# Unit test suite (disabled if we do not have CUnit installed)
if cunit.found()
    test_strfuncs = executable(
        'test-strfuncs',
        [ 'test-strfuncs.c',
          'test-main.c',
          strfuncs_src,
          listfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_header = executable(
        'test-header',
        [ 'test-header.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          digest_src,
          format_src,
          header_src,
          entry_src,
          joinpath_src,
          json_src,
          read_src,
          readfile_src,
          tags_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_joinpath = executable(
        'test-joinpath',
        [ 'test-joinpath.c',
          'test-main.c',
          strfuncs_src,
          listfuncs_src,
          joinpath_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_json = executable(
        'test-json',
        [ 'test-json.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          entry_src,
          joinpath_src,
          json_src,
          tags_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_lead = executable(
        'test-lead',
        [ 'test-lead.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          lead_src,
          entry_src,
          tags_src,
          joinpath_src,
          json_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_mkdirp = executable(
        'test-mkdirp',
        [ 'test-mkdirp.c',
          'test-main.c',
          mkdirp_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_read = executable(
        'test-read',
        [ 'test-read.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          digest_src,
          format_src,
          header_src,
          tags_src,
          read_src,
          readfile_src,
          json_src,
          joinpath_src,
          entry_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_reset = executable(
        'test-reset',
        [ 'test-reset.c',
          'test-main.c',
          reset_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_rpm = executable(
        'test-rpm',
        [ 'test-rpm.c',
          'test-main.c',
          rpm_src,
          strfuncs_src,
          listfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, libarchive, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_signature = executable(
        'test-signature',
        [ 'test-signature.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          signature_src,
          digest_src,
          format_src,
          header_src,
          joinpath_src,
          json_src,
          tags_src,
          entry_src,
          read_src,
          readfile_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_tags = executable(
        'test-tags',
        [ 'test-tags.c',
          'test-main.c',
          tags_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_unpack = executable(
        'test-unpack',
        [ 'test-unpack.c',
          'test-main.c',
          unpack_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, libarchive, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_xalloc = executable(
        'test-xalloc',
        [ 'test-xalloc.c',
          'test-main.c',
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_abspath = executable(
        'test-abspath',
        [ 'test-abspath.c',
          'test-main.c',
          abspath_src,
          joinpath_src,
          listfuncs_src,
          strfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_listfuncs = executable(
        'test-listfuncs',
        [ 'test-listfuncs.c',
          'test-main.c',
          listfuncs_src,
          strfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_xread = executable(
        'test-xread',
        [ 'test-xread.c',
          'test-main.c',
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_strmode = executable(
        'test-strmode',
        [ 'test-strmode.c',
          'test-main.c',
          strmode_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_class = executable(
        'test-class',
        [ 'test-class.c',
          'test-main.c',
          class_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_entry = executable(
        'test-entry',
        [ 'test-entry.c',
          'test-main.c',
          entry_src,
          tags_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_readfile = executable(
        'test-readfile',
        [ 'test-readfile.c',
          'test-main.c',
          readfile_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_digest = executable(
        'test-digest',
        [ 'test-digest.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          digest_src,
          format_src,
          header_src,
          entry_src,
          tags_src,
          joinpath_src,
          json_src,
          read_src,
          readfile_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_format = executable(
        'test-format',
        [ 'test-format.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          digest_src,
          files_src,
          format_src,
          listfuncs_src,
          strfuncs_src,
          header_src,
          entry_src,
          tags_src,
          joinpath_src,
          json_src,
          read_src,
          readfile_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_list = executable(
        'test-list',
        [ 'test-list.c',
          'test-main.c',
          list_src,
          strmode_src,
          strfuncs_src,
          listfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_changelog = executable(
        'test-changelog',
        [ 'test-changelog.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          digest_src,
          format_src,
          header_src,
          entry_src,
          joinpath_src,
          json_src,
          read_src,
          readfile_src,
          tags_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_deps = executable(
        'test-deps',
        [ 'test-deps.c',
          'test-main.c',
          changelog_src,
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          digest_src,
          format_src,
          header_src,
          entry_src,
          joinpath_src,
          json_src,
          read_src,
          readfile_src,
          tags_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, libarchive, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_files = executable(
        'test-files',
        [ 'test-files.c',
          'test-main.c',
          class_src,
          deps_src,
          files_src,
          listfuncs_src,
          strfuncs_src,
          joinpath_src,
          mkdirp_src,
          tags_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_create = executable(
        'test-create',
        [ 'test-create.c',
          'test-main.c',
          abspath_src,
          changelog_src,
          create_src,
          deps_src,
          digest_src,
          entry_src,
          class_src,
          files_src,
          format_src,
          header_src,
          joinpath_src,
          json_src,
          lead_src,
          listfuncs_src,
          read_src,
          readfile_src,
          signature_src,
          strfuncs_src,
          tags_src,
          xalloc_src,
          xread_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc, libarchive, libzstd, openssl, magic ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    # Unit tests
    test('test-strfuncs', test_strfuncs)
    test('test-header', test_header)
    test('test-joinpath', test_joinpath)
    test('test-json', test_json)
    test('test-lead', test_lead)
    test('test-mkdirp', test_mkdirp)
    test('test-read', test_read)
    test('test-reset', test_reset)
    test('test-rpm', test_rpm)
    test('test-signature', test_signature)
    test('test-tags', test_tags)
    test('test-unpack', test_unpack)
    test('test-xalloc', test_xalloc)
    test('test-abspath', test_abspath)
    test('test-listfuncs', test_listfuncs)
    test('test-xread', test_xread)
    test('test-strmode', test_strmode)
    test('test-class', test_class)
    test('test-entry', test_entry)
    test('test-readfile', test_readfile)
    test('test-digest', test_digest)
    test('test-format', test_format)
    test('test-list', test_list)
    test('test-changelog', test_changelog)
    test('test-deps', test_deps)
    test('test-files', test_files)
    test('test-create', test_create)
else
    warning('CUnit not found, skipping unit test suite')
endif
