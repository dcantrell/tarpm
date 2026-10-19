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
libzstd = dependency('libzstd', required : true)

# libmagic
if not cc.has_function('magic_open', args : ['-lmagic'])
    error('*** unable to find magic_open() in libmagic')
endif

magic = declare_dependency(link_args : ['-lmagic'])

# librpm
rpm = dependency('rpm', required : true)

# See commits:
#     db46bd8bd1af3868163a88e73ee9310d896658b7
#     122bb4ffae7b2bacd8d790cac2c765d51b23d7d9
# https://github.com/rpm-software-management/rpm
# (git tag --contains db46bd8bd1af3868163a88e73ee9310d896658b7)
# (git tag --contains 122bb4ffae7b2bacd8d790cac2c765d51b23d7d9)
if rpm.version().version_compare('>=4.19.0')
    add_project_arguments('-D_HAS_UNTRANS_TAG', language : ['c', 'cpp'])
    add_project_arguments('-D_HAS_SPEC_TAG', language : ['c', 'cpp'])
endif

# See commit:
#     b4a3f6c624edb0175855a925f3e0c38e907cb8a9
# https://github.com/rpm-software-management/rpm
# (git tag --contains b4a3f6c624edb0175855a925f3e0c38e907cb8a9)
if rpm.version().version_compare('>=4.16.0')
    add_project_arguments('-D_HAS_META_TAG', language : ['c', 'cpp'])
endif

# See commit:
#     9644bbd5b33c5efdf58898cb8a04ecd39b208be4
#     7e528e66506e48f01acfdc1101591f8084742f2a
# https://github.com/rpm-software-management/rpm
# (git tag --contains 7e528e66506e48f01acfdc1101591f8084742f2a)
# (git tag --contains 9644bbd5b33c5efdf58898cb8a04ecd39b208be4)
if rpm.version().version_compare('>=4.17.0')
    add_project_arguments('-D_HAS_VERITYSIGNATURES_TAG', language : ['c', 'cpp'])
    add_project_arguments('-D_HAS_VERITYSIGNATUREALGO_TAG', language : ['c', 'cpp'])
endif

# See commit:
#     9344367edf654fb41a136793b68cea9abb892fb9
# https://github.com/rpm-software-management/rpm
# (git tag --contains 9344367edf654fb41a136793b68cea9abb892fb9)
if rpm.version().version_compare('>=6.0.0')
    add_project_arguments('-D_HAS_PGPHASHALGO_SHA3_256', language : ['c', 'cpp'])
    add_project_arguments('-D_HAS_PGPHASHALGO_SHA3_512', language : ['c', 'cpp'])
endif

# The main header tags a v6 format package carries.  We write the tag
# names in to header.json and look them back up with rpmTagGetValue(),
# so we can only write a v6 format package when librpm knows the
# names.
# RPMTAG_RPMFORMAT and the SHA-512 and SHA3-256 payload digests all
# arrived with 6.0.0.
# See commit:
#     5a685fb5e51f9f2d5b7e0bbb1d1d5b2e7f9b2c4a
# https://github.com/rpm-software-management/rpm
#
# rpmLeadFromHeader() filled the lead in with the arch and the os of
# the build machine up to version 6.0.0.  Starting with rpm 6.0.0, the
# arch and os values are zero.  We write the lead ourselves, so match
# whichever librpm we were built against to maintain compatibility.
if rpm.version().version_compare('<6.0.0')
    add_project_arguments('-D_LEAD_HAS_ARCH_OS', language : ['c', 'cpp'])
endif

if rpm.version().version_compare('>=6.0.0')
    add_project_arguments('-D_HAS_RPMFORMAT_TAGS', language : ['c', 'cpp'])
endif

# OpenSSL
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
