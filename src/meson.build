# Define source files as File objects so they can be used in unit
# tests.
abspath_src = files('abspath.c')
changelog_src = files('changelog.c')
class_src = files('class.c')
create_src = files('create.c')
deps_src = files('deps.c')
digest_src = files('digest.c')
entry_src = files('entry.c')
extract_src = files('extract.c')
files_src = files('files.c')
format_src = files('format.c')
header_src = files('header.c')
joinpath_src = files('joinpath.c')
json_src = files('json.c')
lead_src = files('lead.c')
list_src = files('list.c')
listfuncs_src = files('listfuncs.c')
main_src = files('main.c')
mkdirp_src = files('mkdirp.c')
read_src = files('read.c')
readfile_src = files('readfile.c')
reset_src = files('reset.c')
rpm_src = files('rpm.c')
signature_src = files('signature.c')
strfuncs_src = files('strfuncs.c')
strmode_src = files('strmode.c')
tags_src = files('tags.c')
unpack_src = files('unpack.c')
xalloc_src = files('xalloc.c')
xread_src = files('xread.c')

sources = [
    abspath_src,
    changelog_src,
    class_src,
    create_src,
    deps_src,
    digest_src,
    entry_src,
    extract_src,
    files_src,
    format_src,
    header_src,
    joinpath_src,
    json_src,
    lead_src,
    list_src,
    listfuncs_src,
    main_src,
    mkdirp_src,
    read_src,
    readfile_src,
    reset_src,
    rpm_src,
    signature_src,
    strfuncs_src,
    strmode_src,
    tags_src,
    unpack_src,
    xalloc_src,
    xread_src,
]

deps = [
    rpm,
    magic,
    libarchive,
    jsonc,
    openssl,
    libzstd
]

tarpm_prog = executable(
    'tarpm',
    sources,
    include_directories : inc,
    dependencies : deps,
    install : true
)
