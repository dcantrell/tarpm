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

A binary package, format 4
==========================

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

    $ tarpm -cvf hello.rpm tree
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

A binary package, format 6
==========================

The same tree.  Pass ``-F 6``::

    $ tarpm -cf hello.rpm -F 6 tree
    $ rpm -qp --qf '%{name}|%{rpmformat}|%{payloadcompressor}|%{payloadflags}\n' hello.rpm
    hello|6|zstd|19

Nothing in ``header.json`` changes.  The format decides which tags
tarpm writes, so the defaults pass puts zstd at level 19 in a format 6
package and gzip at level 9 in a format 4 one, and the format 6 package
gets the Rpmformat, Payloadsize, and extra payload digest tags.  See
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
    gzip at level 9 for a format 4 package, zstd at level 19 for
    format 6.

``Sourcerpm``
    ``(none)`` for a binary package, left off for a source package.

``Sourcenevr``
    The ``Sourcerpm`` value with the ``.src.rpm`` ending taken off.
    Only a format 6 binary package carries this, and only when there
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
trailer rpm(8) reads the package as a format 3 one and says format 3 is
deprecated.

A package with nothing in its payload tree is fine.  rpm takes a
package with no files at all, so tarpm leaves the file list tags out of
the header entirely.

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
``Sourcerpm`` off, and skips the file class and color tags, which
rpmbuild only writes for binary packages.

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
