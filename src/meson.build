# Define source files as File objects so they can be used in unit
# tests.
abspath_src = files('abspath.c')
entry_src = files('entry.c')
extract_src = files('extract.c')
header_src = files('header.c')
joinpath_src = files('joinpath.c')
json_src = files('json.c')
lead_src = files('lead.c')
listfuncs_src = files('listfuncs.c')
main_src = files('main.c')
mkdirp_src = files('mkdirp.c')
read_src = files('read.c')
reset_src = files('reset.c')
rpm_src = files('rpm.c')
signature_src = files('signature.c')
strfuncs_src = files('strfuncs.c')
tags_src = files('tags.c')
unpack_src = files('unpack.c')
xalloc_src = files('xalloc.c')
xread_src = files('xread.c')

sources = [
    abspath_src,
    entry_src,
    extract_src,
    header_src,
    joinpath_src,
    json_src,
    lead_src,
    listfuncs_src,
    main_src,
    mkdirp_src,
    read_src,
    reset_src,
    rpm_src,
    signature_src,
    strfuncs_src,
    tags_src,
    unpack_src,
    xalloc_src,
    xread_src,
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
