install_man('tarpm.1')

# The HTML manual needs doxygen to turn the header files in to XML,
# sphinx-build to turn that and the rst files here in to HTML, and the
# two Python modules conf.py names.  A missing piece skips the manual
# rather than fails the build of the program.
doxygen = find_program('doxygen', required : false)
sphinx_build = find_program('sphinx-build', required : false)
docs_python = find_program(get_option('python_program'), required : false)

if not get_option('docs')
    build_docs = false
elif not (doxygen.found() and sphinx_build.found() and docs_python.found())
    warning('doxygen or sphinx-build not found, skipping the HTML manual')
    build_docs = false
else
    build_docs = run_command(
        docs_python,
        '-c', 'import breathe, sphinx_rtd_theme',
        check : false
    ).returncode() == 0

    if not build_docs
        warning('breathe or sphinx_rtd_theme not found, skipping the HTML manual')
    endif
endif

if build_docs
    # Doxyfile.in leaves the paths open because doxygen resolves them
    # against the directory it runs in.  The filled in copy and
    # everything doxygen writes stay under the meson build directory.
    doxyconf = configuration_data()
    doxyconf.set('INPUT_DIR', meson.project_source_root() / 'include')
    doxyconf.set('OUTPUT_DIR', meson.current_build_dir() / 'doxygen')

    doxyfile = configure_file(
        input : 'Doxyfile.in',
        output : 'Doxyfile',
        configuration : doxyconf
    )

    doxygen_xml = custom_target(
        'doxygen',
        input : doxyfile,
        output : 'doxygen',
        command : [ doxygen, '@INPUT@' ],
        build_by_default : true
    )

    # Everything sphinx reads, so editing any one of them rebuilds the
    # manual.
    docs_sources = files(
        'conf.py',
        'building.rst',
        'creating.rst',
        'extracting.rst',
        'index.rst',
        'listing.rst',
        'metadata.rst',
        'modifying.rst',
        'overview.rst'
    )

    # conf.py names the doxygen XML with a path relative to the source
    # tree, which is wrong for an out of tree build, so point breathe
    # at what the target above just wrote.
    custom_target(
        'html',
        output : 'html',
        command : [
            sphinx_build,
            '-q',
            '-b', 'html',
            '-d', meson.current_build_dir() / 'doctrees',
            '-D', 'breathe_projects.tarpm=' + meson.current_build_dir() / 'doxygen' / 'xml',
            meson.current_source_dir(),
            '@OUTPUT@'
        ],
        depends : doxygen_xml,
        depend_files : docs_sources,
        build_by_default : true
    )
endif
