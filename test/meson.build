# Test setups
add_test_setup(
    'valgrind',
    exe_wrapper : ['valgrind', '--verbose', '--error-exitcode=1', '--leak-check=full', '--track-origins=yes']
)

# Integration test suite
if python.found()
    test_env = environment()
    test_env.set('TARPM', tarpm_prog.full_path())

    test_suites = [
        'test_options.py',
    ]

    foreach suite : test_suites
        test('tarpm-test-suite.' + suite.split('.')[0],
             python,
             args : ['-B', '-m', 'unittest', 'discover', '-v', meson.project_source_root() + '/test', suite],
             env : test_env,
             is_parallel : false,
             timeout : 5000
            )
    endforeach
else
    warning('Python not found, skipping integration test suite')
endif
