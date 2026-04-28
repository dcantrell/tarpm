project('tarpm',
        'c',
        version : '0.1',
        default_options : [
            'c_std=gnu11',
            'warning_level=3',
            'werror=true',
            'buildtype=debugoptimized'
        ],
        license : 'Apache-2.0'
)

cc = meson.get_compiler('c')

# Define the program version
add_global_arguments('-DPACKAGE_VERSION="@0@"'.format(meson.project_version()), language : 'c')

# Always add _GNU_SOURCE because some other libraries rely on this macro
add_global_arguments('-D_GNU_SOURCE', language : 'c')

# Define this to get around a problematic json_object.h macro
add_global_arguments('-D__STRICT_ANSI__', language : 'c')

# See if we have reallocarray in libc
if cc.has_function('reallocarray')
    add_global_arguments('-D_HAVE_REALLOCARRAY', language : 'c')
endif

# Translations
if find_program('xgettext', required : get_option('nls')).found()
    add_global_arguments('-DGETTEXT_DOMAIN="' + meson.project_name() + '"', language : 'c')
    subdir('po')
endif

# Dependencies
jsonc = dependency('json-c', required : true)
libarchive = dependency('libarchive', required : true)

if cc.has_function('archive_version_details', dependencies : [ libarchive ])
    add_project_arguments('-D_HAVE_ARCHIVE_VERSION_DETAILS', language : ['c', 'cpp'])
elif cc.has_function('archive_version_string', dependencies : [ libarchive ])
    add_project_arguments('-D_HAVE_ARCHIVE_VERSION_STRING', language : ['c', 'cpp'])
endif

rpm = dependency('rpm', required : true)

if rpm.version().version_compare('<4.15.0')
    add_project_arguments('-D_HAVE_OLD_RPM_API', language : ['c', 'cpp'])
endif

# Test suite dependencies
run_tests = get_option('tests')
if run_tests
    python = find_program(get_option('python_program'), required : false)
else
    python = disabler()
endif

# Header files
inc = include_directories('include')

# Include all relevant subdirectories
subdir('src')
subdir('test')
