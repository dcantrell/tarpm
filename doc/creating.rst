===============================
Creating a package from scratch
===============================

You do not have to extract a package to create one.  A directory
holding a ``header.json`` you wrote and a ``payload`` subdirectory is
enough.  tarpm walks the payload tree, builds the whole file list from
what it finds, and fills in the header tags a package needs but you
have no reason to name.

Only three tags have no value tarpm can work out, which are ``Name``,
``Version``, and ``Release``.  A header missing any of them is an
error.  ``License`` has no sensible default either, so tarpm warns
rather than invent a license string, and carries on.  ``Summary`` is
worth writing as well, since queries look wrong without it.

A missing input directory, or one tarpm cannot read, makes tarpm exit
non-zero.

tarpm writes the package to a temporary file named ``.tarpm-XXXXXX`` in
the directory of the output file and renames it over the output file
when it is complete.  You need write access to that directory.  A
package that was already there keeps its owner and mode if you can set
them.  A symlink at the output path is replaced, not followed.  Output
to something that is not a regular file, such as a device, is written
directly.

If writing the package fails, for example on a full filesystem, tarpm
exits non-zero and removes the temporary file.  The same goes for a
payload file tarpm cannot read, and for one that changes size while
tarpm builds the package.  A package that was already there stays as it
was.

A binary package, v4 format
===========================

Build the tree::

    $ mkdir -p tree/payload/usr/bin
    $ printf '#!/bin/sh\necho hello\n' > tree/payload/usr/bin/hello
    $ chmod 755 tree/payload/usr/bin/hello

Write ``tree/header.json``::

    {
      "tags": [
        { "tag": "Name",    "type": "string",      "value": "hello" },
        { "tag": "Version", "type": "string",      "value": "1.0" },
        { "tag": "Release", "type": "string",      "value": "1" },
        { "tag": "Summary", "type": "i18n string", "value": "A greeting" },
        { "tag": "License", "type": "string",      "value": "MIT" }
      ]
    }

Create the package::

    $ tarpm -cf hello.rpm tree
    tarpm: *** /usr is new in the payload, adding it to the file list
    tarpm: *** /usr/bin is new in the payload, adding it to the file list
    tarpm: *** /usr/bin/hello is new in the payload, adding it to the file list

That is an installable package::

    $ rpm -qp --qf '%{name}-%{version}-%{release}.%{arch}\n' hello.rpm
    hello-1.0-1.x86_64
    $ rpm -qlp hello.rpm
    /usr
    /usr/bin
    /usr/bin/hello
    $ rpm --define '_pkgverify_level digest' -K hello.rpm
    hello.rpm: digests OK

Note that the directories above the file are in the package.  In a tree
you wrote from nothing, everything in the payload is something you put
there, so tarpm takes all of it to be owned by the package.  Delete the
entries for ``/usr`` and ``/usr/bin`` from ``header.json`` afterwards
if you do not want to own them, or extract and repack once to get the
same effect.

A binary package, v6 format
===========================

The same tree.  Pass ``-F 6``::

    $ tarpm -cf hello.rpm -F 6 tree
    $ rpm -qp --qf '%{name}|%{rpmformat}|%{payloadcompressor}|%{payloadflags}\n' hello.rpm
    hello|6|zstd|19

Nothing in ``header.json`` changes.  The format decides which tags
tarpm writes, so the v6 format package gets the Rpmformat, Payloadsize,
and extra payload digest tags.  ``header.json`` names no compressor, so
the defaults pass gives the v6 format package zstd at level 19 and the
v4 format one gzip at level 9.  Each file gets a MIME type in place of a
class, and the payload is a stripped cpio archive.  See
the tarpm(1) man page for the full list of which tags belong to which
format.

What tarpm fills in
===================

A tag the header already carries is never touched, which is what lets
an extracted tree go back out unchanged.  These are the defaults:

``Os``
    ``linux``

``Arch``
    The machine name from uname(2), or ``src`` for a source package.

``Payloadformat``
    ``cpio``, which is the only format rpm takes.

``Payloadcompressor``, ``Payloadflags``
    gzip at level 9 for a v4 format package or a source package, zstd
    at level 19 for a v6 format binary package.  These match the rpm(8)
    defaults.  A compressor the metadata names is kept in either
    format.  A v6 format package that names a compressor other than
    zstd gets level 9.  The names are ``gzip``, ``bzip2``, ``xz``, ``lzma``,
    ``zstd``, and ``none``.  tarpm refuses any other name.  A gzip
    payload carries an mtime of 0 in its gzip header, the same as
    rpm(8) writes.  An xz payload carries a SHA-256 check, the same as
    rpm(8) writes.  xz takes levels 0 to 9.

    ``Payloadflags`` is the compression level, with an optional ``T``
    and thread count after it, such as ``3T`` or ``19T8``.  tarpm uses
    the level and ignores the thread count.  Any other value is an
    error, and so is a level the compressor does not take.

    ``none`` gives an uncompressed payload.  rpm(8) has no name for
    this, so the package carries no ``Payloadcompressor`` tag and
    ``Payloadflags`` is 0, the same as rpmbuild(8) writes.  Extracting
    an uncompressed package writes ``none`` to ``header.json``.

``Sourcerpm``
    ``(none)`` for a binary package, left off for a source package.

``Sourcenevr``
    The ``Sourcerpm`` value with the ``.src.rpm`` ending taken off.
    Only a v6 format binary package carries this, and only when there
    is a source package to name.

``Filedigestalgo``
    ``sha256``.

``Buildtime``
    Now.

``Buildhost``
    The name from gethostname(2).

``Summary``, ``Description``
    The ``Name`` value.

``Headerimmutable``
    The region trailer.

``Filedigestalgo`` is the one exception to the never touch rule being
the whole story.  If the tree already describes its files, a missing
``Filedigestalgo`` means md5, because that is how rpm reads it and the
digests already in the tree were made that way.  tarpm only writes the
sha256 default when it is digesting the files itself.

The package also gets the provide it carries for itself, which is its
name at its own EVR, unless ``dependencies`` already provides the name.

``Headerimmutable`` matters more than it looks.  Without the region
trailer rpm(8) reads the package as a v3 format one and says the v3
format is deprecated.  The header can hold only one region tag.  tarpm
refuses a ``tags`` array with a second ``Headerimmutable`` entry, or
with a ``Headersignatures`` entry, which belongs in the signature
header.

A package with nothing in its payload tree is fine.  rpm takes a
package with no files at all, so tarpm leaves the file list tags out of
the header entirely.

Watching it work
================

``-v`` prints each stage of the build as it happens.  Each stage line
starts with ``>>>``.  The details of each stage are indented two
spaces under it.  Here is the tree from above::

    $ tarpm -cvf hello.rpm tree
    >>> Creating /home/you/hello.rpm, a v4 format package
      from /home/you/tree
    >>> Reading header.json
      5 tags, 0 files, 0 dependencies, 0 changelog entries
    >>> Filling in the tags header.json does not carry
      Os: linux
      Arch: x86_64
      Payloadformat: cpio
      Payloadcompressor: gzip
      Payloadflags: 9
      Sourcerpm: (none)
      Filedigestalgo: sha256
      Buildtime: 2026-11-11T12:00:00Z
      Buildhost: build.example.com
      Description: hello
      Provides: hello = 1.0-1
      Headerimmutable: the region trailer
    >>> Scanning the payload tree /home/you/tree/payload
      /usr: new in the payload, added to the file list
      /usr/bin: new in the payload, added to the file list
      /usr/bin/hello: new in the payload, added to the file list
      3 new entries added to the file list
    >>> Fitting the header to the v4 format
      adding Payloadsha256algo
      adding Payloadsha256
      adding Payloadsha256alt
    >>> Building the lead
    >>> Building the file list tags for 3 files
    >>> Writing the payload as a newc cpio archive, gzip compressed at level 9
      ./usr
      ./usr/bin
      ./usr/bin/hello
      3 payload entries
    >>> Computing the payload digests
      Payloadsha256: db05c35d3536888a13b24b725200039c11945e23530481582e093bc308b1ce39
      Payloadsha256alt: bbb16063769728d61716853b0ca498171496c84c21114edcd7302283b74cfc72
      137 bytes compressed, 512 bytes uncompressed
    >>> Building the main header
      42 tags, 1.2 KiB
    >>> Building the signature header
      7 tags, 4.3 KiB
    >>> Writing /home/you/hello.rpm
    >>> Done, the package is 5.8 KiB

This is what the stages tell you:

* The defaults stage lists each default from the list above that went
  in, with its value.  It says ``nothing was missing`` when
  ``header.json`` already had them all.
* The payload tree stage names each file that is in the payload but
  not in the ``files`` array, then counts them.  With ``-v`` this line
  takes the place of the warning tarpm prints without it.
* ``Using data as-is`` shows up for raw changelog tags in
  ``header.json`` and for ``-D`` and ``-k``.  tarpm does not check this
  data.  A digest kept this way is wrong for the package if the payload
  changed.
* The format stage names the tags the format drops, the size tags that
  change spelling, and the tags it adds.
* The file list stage names each file whose size or contents changed
  since ``header.json`` was written.  With ``-D`` it says when it keeps
  a digest that no longer matches the file.  It also names each file in
  the ``files`` array that is gone from the payload tree and left out of
  the package.  That line takes the place of the warning too.
* The payload stage names the archive format and the compression, then
  prints each entry as it goes in, the same way ``tar -cv`` does.
* The digest stage prints each payload digest and the compressed and
  uncompressed sizes.  A digest that ``-D`` kept says so.
* The two header stages give the tag count and the size of each
  header.  With ``-D`` and ``-k`` the signature header stage names each
  digest and signature taken from ``signature.json``.

The status lines go to standard output and the warnings go to standard
error.

Dependencies
============

tarpm runs no dependency generators.  A package you build from scratch
provides itself and requires nothing, which is wrong for anything
linked against a shared library.

The generators rpmbuild uses are ordinary programs that read paths on
standard input, so point them at the payload tree::

    $ find tree/payload -type f | /usr/lib/rpm/find-requires | sort -u
    libc.so.6()(64bit)
    libc.so.6(GLIBC_2.11)(64bit)
    libc.so.6(GLIBC_2.14)(64bit)
    ...

    $ find tree/payload -type f | /usr/lib/rpm/find-provides | sort -u

Then write what comes back into ``dependencies``::

    "dependencies": {
      "requires": [
        { "name": "libc.so.6()(64bit)" },
        { "name": "libc.so.6(GLIBC_2.14)(64bit)" }
      ],
      "provides": [
        { "name": "hello", "comparison": "=", "version": "1.0-1" }
      ]
    }

A few notes on doing this by hand:

* The generators print one dependency per line with no version
  comparison, so each one is a dict with just a ``name``.
* Sort and uniq the output.  The generators repeat themselves across
  files.
* You do not need ``sense_flags``.  Those record how rpmbuild came by
  the dependency and nothing enforces them.
* ``rpmlib()`` requires are up to you.  rpm(8) will install a package
  without them.  Add them if you want the package to refuse to install
  on an rpm too old for a feature it uses.
* Nothing validates any of this.  A requires you spell wrong is an
  unsatisfiable dependency at install time.

A source package
================

Why would you want one?  A source package is not a build input the way
a spec file is.  It is a container holding the spec, the tarballs, and
the patches, with just enough header to say what is in it.  Reasons to
build one with tarpm rather than rpmbuild:

* you have the sources and the spec but not the build dependencies, so
  ``rpmbuild -bs`` will not run
* you want to change one patch in an existing source package without
  touching anything else
* you are generating source packages from something that is not a spec
  file tree, in a script

What makes a package a source package is the ``Sourcepackage`` tag.
Set it and tarpm writes ``src`` for the architecture, leaves
``Sourcerpm`` off, and skips the file class, MIME type, and color tags,
which rpmbuild only writes for binary packages.

The payload is flat.  The spec file, the tarballs, and the patches all
sit at the top of the payload directory with no subdirectories, and the
file list paths have no leading slash because none of it installs
anywhere.

Build the tree::

    $ mkdir -p tree/payload
    $ cp hello.spec hello-1.0.tar.gz tree/payload/

Write ``tree/header.json``::

    {
      "tags": [
        { "tag": "Name",          "type": "string",       "value": "hello" },
        { "tag": "Version",       "type": "string",       "value": "1.0" },
        { "tag": "Release",       "type": "string",       "value": "1" },
        { "tag": "Summary",       "type": "i18n string",  "value": "A greeting" },
        { "tag": "License",       "type": "string",       "value": "MIT" },
        { "tag": "Sourcepackage", "type": "int32",        "value": 1 },
        { "tag": "Spec",          "type": "string",       "file": "payload/hello.spec" },
        { "tag": "Source",        "type": "string array", "value": [ "hello-1.0.tar.gz" ] }
      ]
    }

Two of those are specific to source packages.  ``Spec`` holds the whole
spec file as a header value, and the ``file`` key points at the copy in
the payload so you are not keeping two of them in step.  ``Source``
lists the source file names, and a ``Patch`` tag of the same shape
lists the patch file names.

Create it::

    $ tarpm -cf hello-1.0-1.src.rpm tree
    $ rpm -qp --qf '%{name}|%{arch}|%{sourcepackage}\n' hello-1.0-1.src.rpm
    hello|src|1
    $ rpm -qlp hello-1.0-1.src.rpm
    hello-1.0.tar.gz
    hello.spec

rpm(8) unpacks it the way it unpacks any source package, with the spec
going to ``SPECS`` and everything else to ``SOURCES``::

    $ rpm -i --define "_topdir $PWD/topdir" hello-1.0-1.src.rpm
    $ find topdir
    topdir
    topdir/SOURCES
    topdir/SPECS
    topdir/SOURCES/hello-1.0.tar.gz
    topdir/SPECS/hello.spec

From there ``rpmbuild -ba topdir/SPECS/hello.spec`` builds it.

What goes in the spec file
--------------------------

Whatever a normal spec file holds.  tarpm does not read it, parse it,
or check that it agrees with the header, so nothing here is a tarpm
requirement.  The thing to watch is that rpmbuild will read it later
and you are the one keeping the two in step.  Make the ``Name``,
``Version``, and ``Release`` in the spec match the tags in
``header.json``, and make the ``Source`` and ``Patch`` lines name the
files you actually put in the payload.

A minimal one that rebuilds::

    Name:           hello
    Version:        1.0
    Release:        1
    Summary:        A greeting
    License:        MIT
    Source0:        hello-1.0.tar.gz

    %description
    A greeting.

    %prep
    %autosetup

    %build
    %make_build

    %install
    %make_install

    %files
    /usr/bin/hello
