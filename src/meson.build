sources = [
    'entry.c',
    'header.c',
    'init.c',
    'joinpath.c',
    'json.c',
    'lead.c',
    'main.c',
    'mkdirp.c',
    'read.c',
    'rpm.c',
    'signature.c',
    'strfuncs.c',
    'tags.c',
    'unpack.c',
    'xalloc.c',
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
