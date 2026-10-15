====================
Extracting a package
====================

The basics
==========

::

    $ tarpm -xvf bash-5.3.9-3.fc44.x86_64.rpm

That makes a directory named after the NEVRA of the package in the
directory you are in.  Use ``-O`` to put it somewhere else::

    $ tarpm -xvf bash-5.3.9-3.fc44.x86_64.rpm -O /var/tmp/bash

Either way you end up with this::

    bash-5.3.9-3.fc44.x86_64/
    |-- lead.json
    |-- signature.json
    |-- header.json
    |-- description.txt
    |-- postin.txt
    |-- postun.txt
    `-- payload/
        |-- etc/
        `-- usr/

The ``payload`` subdirectory is the files of the package, laid out the
way they land on an installed system.  The JSON files are the three
binary structures in front of the payload, turned into something you can
read.  The ``.txt`` files hold the value of a few long text tags.  See
:doc:`metadata` for what is in each one.

Extraction runs as you, so the files in ``payload`` are owned by you no
matter what the package says.  The real owner and group stay in the
header, and that is what tarpm writes back out when you create a
package.  Do not run ``chown`` over the tree to make it look right.

One case does need root.  A package carrying a device node cannot be
extracted by an ordinary user, because writing the node means calling
mknod(2).  tarpm says so and exits non-zero::

    $ tarpm -xf devtest-1.0-1.noarch.rpm
    tarpm: archive_write_header: Can't create 'dev/testnode'
    tarpm: archive_write_header: Can't create 'dev/testnode'
    tarpm: unpack_archive

The message comes twice because tarpm has two ways to read a payload
and tries both.  See `The payload`_ below.

The JSON still gets written, so this is enough to read the metadata.
Do not repack from a tree like that, though.  The device node is not in
the payload tree, so tarpm takes it to be a file you deleted and leaves
it out of the new package.  Extract as root if you mean to repack.

Sending pieces elsewhere
========================

Each output has its own option.  ``-L``, ``-S``, and ``-H`` name where
``lead.json``, ``signature.json``, and ``header.json`` go, and ``-P``
names the payload directory::

    $ tarpm -xf bash.rpm -H /var/tmp/bash-header.json -P /var/tmp/bashfiles

A path of a single hyphen writes the JSON to standard output, which is
the quick way to pipe one piece into something else::

    $ tarpm -xf bash.rpm -H - | jq '.tags[] | select(.tag == "Version")'

tarpm only makes a directory when a file is about to go in it.  Name a
home for all four pieces and the output directory never happens::

    $ tarpm -xf bash.rpm -L l.json -S s.json -H h.json -P ./files
    $ ls -d bash-5.3.9-3.fc44.x86_64
    ls: cannot access 'bash-5.3.9-3.fc44.x86_64': No such file or directory

The same goes the other way.  A ``-H`` path several directories deep
gets those directories made for it when the file is written, so you do
not have to set them up first.

That example still unpacks the payload, which is wasted work when all
you want is the header.  ``-n`` turns the payload off.

Metadata only
=============

``-n`` writes the JSON and skips the payload.  No ``payload``
subdirectory is made and the cpio archive is never read::

    $ tarpm -xnf bash-5.3.9-3.fc44.x86_64.rpm -O meta
    $ ls meta
    description.txt  header.json  lead.json  postin.txt  postun.txt  signature.json

The metadata is byte for byte what a full extraction writes.  The
``files`` array still describes every file the package owns, because
that comes out of the header rather than off the payload tree.  The
tag values that go to their own file are still written too, for the
same reason.

For scripting, pair it with ``-H -``::

    $ tarpm -xnf bash.rpm -O meta -H - | jq -r '.files[] | select(.flags) | .path'

That still makes the output directory, though.  ``lead.json``,
``signature.json``, and the tag value files have nowhere else to go,
so they land there::

    $ ls meta
    description.txt  lead.json  postin.txt  postun.txt  signature.json

Send the other two somewhere as well and all that is left is the tag
value files::

    $ tarpm -xnf bash.rpm -O meta -L /dev/null -S /dev/null -H -
    $ ls meta
    description.txt  postin.txt  postun.txt

Only one piece at a time should go to ``-``.  Three of them writing to
standard output gives you three JSON documents back to back, which no
parser will take.

``-n`` is also the way to read a package you cannot otherwise unpack.
A package carrying device nodes needs root to extract, as above, but
reading its metadata does not::

    $ tarpm -xnf devtest-1.0-1.noarch.rpm -O meta
    $ jq -r '.files[] | "\(.path) \(.type) \(.rdev)"' meta/header.json
    /dev/testnode chardev 259

``-P`` has nothing to say when there is no payload to write, so tarpm
says so and carries on.

The payload
===========

You do not have to care about this to use tarpm, but it explains the
output when something goes wrong.

The payload is a cpio archive and rpm writes it two different ways.
The usual one is newc, which every cpio tool reads.  A package whose
header carries Longfilesizes gets what rpm calls a stripped archive
instead, with a magic of ``07070X`` and a file index in each entry in
place of the name, the mode, and the timestamps, since the main header
already holds all of that.  Every format 6 package is written that
way, and so is a format 4 package holding a file too large to count in
32 bits.

So tarpm has two readers.  It hands the package to libarchive first,
which is quick but only knows newc.  When that does not work it falls
back to librpm, which knows both, writes the payload out to a tar file
of its own next to the package, and unpacks that.

That is why a failure in the payload can print the same complaint
twice, once per reader, and why the last line names whichever reader
gave up last.

Reading what you got
====================

A source package looks a little different.  The payload is the spec
file, the tarballs, and the patches, all at the top level with no
directories.  The file list paths have no leading slash because those
files do not install anywhere.  The spec goes to ``spec.txt`` as well as
into the payload.

In a binary package, start with the ``tags`` array in ``header.json``.
That is Name, Version, Release, Summary, License, and everything else
rpmbuild recorded.  Then:

* ``files`` is one dict per file the package owns.  Everything RPM knows
  about a file is in that one dict instead of spread across two dozen
  parallel arrays.
* ``dependencies`` is provides, requires, conflicts, obsoletes, and the
  weak dependency types, written the way you would write them in a spec
  file.
* ``changelog`` is one dict per changelog block.

Some things are worth knowing before you start reading values.

Timestamps are ISO 8601 in UTC
   ``"mtime": "2026-01-16T00:00:00Z"`` rather than a time_t.  Changelog
   dates use the format the spec file uses, such as
   ``"Fri Jan 16 2026"``.

Bitfields are arrays of names
   ``flags``, ``verifyflags``, ``colors``, and the dependency
   ``sense_flags`` all come out as arrays of strings.  You do not have
   to look up bit values.

Numbers that name something are strings
   The file digest algorithm is ``"sha256"``, not ``8``.  File modes are
   four digit octal strings such as ``"0755"``, with the file type split
   out into its own ``type`` key.

Read-only things say so
   Anything carrying ``"read-only": "true"`` is there to look at.  tarpm
   recomputes it when creating a package and does not read what you
   wrote.  Everything in ``lead.json`` and ``signature.json`` is marked
   this way.

Missing keys mean nothing to say
   A file list entry only carries the keys that apply to it.  A
   directory has no ``digest``, a regular file has no ``linkto``, and
   plenty of files have no ``class``.  That is normal.

Two useful things to do with an extracted tree:

* ``diff -u`` the ``header.json`` of two builds of the same package to
  see what actually changed between them
* run ``jq`` over ``files`` to answer questions the ``rpm -qp``
  query format makes awkward, like which files are config and
  noreplace and owned by a user other than root
