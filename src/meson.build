sources = [
    'main.c',
    'init.c',
    'rpm.c',
    'strfuncs.c',
    'xalloc.c',
    'mkdirp.c',
    'unpack.c',
    'header.c',
]

deps = [
    rpm,
    libarchive,
]

tarpm_prog = executable(
    'tarpm',
    sources,
    include_directories : inc,
    dependencies : deps
)
