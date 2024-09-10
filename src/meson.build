sources = [
    'main.c',
    'init.c',
    'rpm.c',
    'strfuncs.c',
    'xalloc.c',
    'mkdirp.c',
    'unpack.c',
    'lead.c',
    'header.c',
    'signature.c',
    'joinpath.c',
]

deps = [
    rpm,
    libarchive,
    jsonc,
]

tarpm_prog = executable(
    'tarpm',
    sources,
    include_directories : inc,
    dependencies : deps
)
