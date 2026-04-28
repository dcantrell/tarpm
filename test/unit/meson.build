# Unit test suite (disabled if we do not have CUnit installed)
if cunit.found()
    test_strfuncs = executable(
        'test-strfuncs',
        [ 'test-strfuncs.c',
          'test-main.c',
          strfuncs_src,
          xalloc_src,
        ],
        include_directories : inc,
        dependencies : [ cunit, jsonc ],
        c_args : '-D_BUILDDIR_="@0@"'.format(meson.current_build_dir()),
    )

    # Unit tests
    test('test-strfuncs', test_strfuncs)
else
    warning('CUnit not found, skipping unit test suite')
endif
