# Integration test suite
if python.found()
    test_env = environment()
    test_env.set('TARPM', tarpm_prog.full_path())

    test_suites = [
        'test_options.py',
        'test_lead.py',
        'test_signature.py',
        'test_header.py',
        'test_extract.py',
    ]

    foreach suite : test_suites
        test('tarpm-test-suite.' + suite.split('.')[0],
             python,
             args : ['-B', '-m', 'unittest', 'discover', '-v', meson.current_source_dir(), suite],
             env : test_env,
             is_parallel : false,
             timeout : 5000
            )
    endforeach
else
    warning('Python not found, skipping integration test suite')
endif
