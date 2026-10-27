# Integration test suite
if python.found()
    # makes one fwrite() call fail, see failfwrite.c
    failfwrite = shared_module('failfwrite',
                               'failfwrite.c',
                               dependencies : cc.find_library('dl', required : false),
                               build_by_default : false)

    test_env = environment()
    test_env.set('TARPM', tarpm_prog.full_path())
    test_env.set('FAILFWRITE', failfwrite.full_path())

    test_suites = [
        'test_options.py',
        'test_list.py',
        'test_lead.py',
        'test_signature.py',
        'test_header.py',
        'test_format.py',
        'test_extract.py',
        'test_create.py',
        'test_class.py',
        'test_mime.py',
        'test_roundtrip.py'
    ]

    foreach suite : test_suites
        test('tarpm-test-suite.' + suite.split('.')[0],
             python,
             args : ['-B', '-m', 'unittest', 'discover', '-v', meson.current_source_dir(), suite],
             env : test_env,
             depends : failfwrite,
             is_parallel : false,
             timeout : 5000
            )
    endforeach
else
    warning('Python not found, skipping integration test suite')
endif
