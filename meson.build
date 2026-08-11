project('tarpm',
        'c',
        version : '0.1',
        default_options : [
            'c_std=gnu11',
            'warning_level=3',
            'werror=true',
            'buildtype=debugoptimized'
        ],
        license : 'GPL-3.0-or-later AND LGPL-3.0-or-later AND Apache-2.0 AND BSD-3-Clause'
)

fs = import('fs')

cc = meson.get_compiler('c')

# Header search dirs
if build_machine.system() == 'netbsd'
    include_dirs = [ '/usr/local/include', '/usr/pkg/include', '/usr/include' ]
else
    include_dirs = [ '/usr/local/include', '/usr/include' ]
endif
existing_include_dirs = []
foreach dir : include_dirs
    if fs.is_dir(dir)
        existing_include_dirs += dir
    endif
endforeach
sysinc = include_directories(existing_include_dirs)

# Define the program version
add_global_arguments('-DPACKAGE_VERSION="@0@"'.format(meson.project_version()), language : 'c')

# Always add _GNU_SOURCE because some other libraries rely on this macro
add_global_arguments('-D_GNU_SOURCE', language : 'c')

# Define this to get around a problematic json_object.h macro
add_global_arguments('-D__STRICT_ANSI__', language : 'c')

# Check for reallocarray in libc
if cc.has_function('reallocarray')
    add_global_arguments('-D_HAVE_REALLOCARRAY', language : 'c')
endif

# Check for strlcpy in libc
if cc.has_function('strlcpy')
    add_global_arguments('-D_HAVE_STRLCPY', language : 'c')
endif

# Check for sys/queue.h
if not cc.has_header('sys/queue.h', include_directories : sysinc)
    message('<sys/queue.h> not found, using bundled copy')
    add_project_arguments('-D_COMPAT_QUEUE', language : ['c', 'cpp'])
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

libzstd = dependency('libzstd', required : true)
rpm = dependency('rpm', required : true)

if rpm.version().version_compare('<4.15.0')
    add_project_arguments('-D_HAVE_OLD_RPM_API', language : ['c', 'cpp'])
endif

openssl = dependency('openssl', required : true)

# Test suite dependencies
run_tests = get_option('tests')
if run_tests
    cunit = dependency('cunit', method : 'pkg-config', required : true)
    python = find_program(get_option('python_program'), required : false)
else
    cunit = disabler()
    python = disabler()
endif

# Header files
inc = include_directories('include')

# Include all relevant subdirectories
subdir('src')
subdir('test')
subdir('doc')
