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

The NEVRA comes from the package header.  tarpm refuses to use it as
the directory name if it holds a slash or a control character, or if
it is ``.`` or ``..``.  A crafted package could otherwise send the
extraction outside the directory you are in.  Use ``-O`` to extract
such a package.

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

A v6 format package prints ``tarpm: unpack_rpm_payload`` after that,
because librpm reads its payload.  See `The payload`_ below.

The JSON still gets written, so this is enough to read the metadata.
Do not repack from a tree like that, though.  The device node is not in
the payload tree, so tarpm takes it to be a file you deleted and leaves
it out of the new package.  Extract as root if you mean to repack.

tarpm does not restore the setuid and setgid bits on files and
directories in ``payload``.  Device nodes get mode 0600.  The full modes
stay in the header, and that is what tarpm writes back out when you
create a package.

tarpm does not write through a symlink.  If ``header.json`` or one of
the other JSON or ``.txt`` files is already a symlink in the output
directory, tarpm says so and exits non-zero.

tarpm refuses a package whose header does not hold together, and exits
non-zero.  A header with more than 65535 entries or more than 256 MiB
of data is refused, as is one with an entry that points outside the
header data or a string with no terminating NUL.  These are the limits
rpm(8) uses.

The file list and changelog tags have to agree with each other for
tarpm to build the ``files`` and ``changelog`` arrays.  If they do not,
tarpm says so and keeps those tags as they are under ``tags`` in
``header.json``.  The rest of the metadata is still written, and tarpm
exits non-zero::

    tarpm: *** changelog arrays have mismatched lengths
    tarpm: *** unable to build the changelog array, keeping the raw tags in header.json

A bad file list can also stop the payload from unpacking, because
librpm needs the file list for its reader.  tarpm will not create a
package from raw tags like these.  See :doc:`metadata`.

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

The tag value files such as ``description.txt`` go next to the file
``-H`` names, since the ``file`` keys in ``header.json`` name them
relative to it.

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
already holds all of that.  Every v6 format package is written that
way, and so is a v4 format package holding a file too large to count
in 32 bits.

So tarpm has two readers.  libarchive is quick but only knows newc.
librpm knows both.  A header with Longfilesizes means a stripped
archive, so tarpm hands that package straight to librpm.  Any other
package goes to libarchive first, and to librpm only when libarchive
cannot read a single entry.  librpm reads the entries and tarpm writes
each one to the payload tree as it goes, the same way libarchive does.
Nothing is written anywhere else, so ``$TMPDIR`` is not used.  Files
that are hard links of one another in the package come out as hard
links from either reader.

An entry that fails after others went in does not start the second
reader.  A device node a regular user cannot make or a full disk is
reported once, the rest of the payload still goes in, and tarpm exits
non-zero.  Only a payload libarchive cannot read at all can print a
complaint from each reader.

A payload that is corrupt or cut short makes tarpm exit non-zero.

Watching it work
================

``-v`` prints each stage of the extract as it happens.  Each stage
line starts with ``>>>``.  The details of each stage are indented two
spaces under it::

    $ tarpm -xvf hello.rpm
    >>> Extracting /home/you/hello.rpm
      hello-1.0-1.x86_64, a v4 format binary package
      in to /home/you/hello-1.0-1.x86_64
    >>> Reading the lead
      lead version 3.0 for a binary package
    >>> Reading the signature header
      7 tags
    >>> Reading the main header
      20 tags, 3 files, 1 dependency, 0 changelog entries
    >>> Writing the metadata
      lead.json
      signature.json
      header.json
    >>> Unpacking the gzip compressed payload in to /home/you/hello-1.0-1.x86_64/payload
      reading the cpio archive with libarchive
      ./usr
      ./usr/bin
      ./usr/bin/hello
      3 payload entries, 21 bytes installed size

Each payload entry gets a line as it lands on disk, the same way
``tar -xv`` prints them.  The line under the payload stage names the
reader.  When libarchive cannot read the payload, another line says
tarpm is trying librpm.  The last line counts the entries and gives the
installed size the header records.  With ``-n`` the payload stage says
the payload was skipped.

The status lines go to standard output.  When ``-L``, ``-S``, or ``-H``
sends JSON to standard output, the status lines go to standard error
instead.  That keeps the JSON clean for a pipe, the same as
``tar -cvf -`` does with its file list.  Warnings always go to standard
error.  Paths in the status lines are escaped the same way as in the
``-t`` output.

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
   ``"Fri Jan 16 2026"``.  A changelog date that is not noon UTC also
   carries the time, such as ``"Thu Oct 06 06:48:39 UTC 2016"``.

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
