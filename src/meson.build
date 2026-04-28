# Define source files as File objects so they can be used in unit
# tests.
entry_src = files('entry.c')
header_src = files('header.c')
init_src = files('init.c')
joinpath_src = files('joinpath.c')
json_src = files('json.c')
lead_src = files('lead.c')
main_src = files('main.c')
mkdirp_src = files('mkdirp.c')
read_src = files('read.c')
rpm_src = files('rpm.c')
signature_src = files('signature.c')
strfuncs_src = files('strfuncs.c')
tags_src = files('tags.c')
unpack_src = files('unpack.c')
write_src = files('write.c')
xalloc_src = files('xalloc.c')

sources = [
    entry_src,
    header_src,
    init_src,
    joinpath_src,
    json_src,
    lead_src,
    main_src,
    mkdirp_src,
    read_src,
    rpm_src,
    signature_src,
    strfuncs_src,
    tags_src,
    unpack_src,
    write_src,
    xalloc_src,
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
