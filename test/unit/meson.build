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
          header_src,
          entry_src,
          joinpath_src,
          json_src,
          read_src,
          tags_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_joinpath = executable(
        'test-joinpath',
        [ 'test-joinpath.c',
          'test-main.c',
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
          entry_src,
          joinpath_src,
          json_src,
          tags_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_lead = executable(
        'test-lead',
        [ 'test-lead.c',
          'test-main.c',
          lead_src,
          entry_src,
          tags_src,
          joinpath_src,
          json_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm ],
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
          header_src,
          tags_src,
          read_src,
          json_src,
          joinpath_src,
          entry_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc, rpm ],
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
          signature_src,
          header_src,
          joinpath_src,
          json_src,
          tags_src,
          entry_src,
          read_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, rpm, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    test_tags = executable(
        'test-tags',
        [ 'test-tags.c',
          'test-main.c',
          tags_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
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
else
    warning('CUnit not found, skipping unit test suite')
endif
