=================
The JSON metadata
=================

This is the reference for what tarpm writes and reads.  Everything here
is a view of the binary structures in the RPM file, so it helps to know
what those look like first.

What an RPM file is
===================

Four pieces, one after the other::

    +---------------------+
    | lead (96 bytes)     |  obsolete, fixed size
    +---------------------+
    | signature header    |  digests and signatures
    +---------------------+
    | header              |  everything else
    +---------------------+
    | payload             |  compressed cpio archive
    +---------------------+

Both headers use the same binary layout.  An 8 byte magic, the number
of index entries, the size of the data area, then that many 16 byte
index entries, then the data area itself.  Each index entry is a tag
number, a type, an offset into the data area, and a count::

    struct entry {
        int32_t tag;      /* RPMTAG_NAME, RPMTAG_VERSION, ... */
        int32_t type;     /* RPM_STRING_TYPE, RPM_INT32_TYPE, ... */
        int32_t offset;   /* where the value sits in the data area */
        int32_t count;    /* how many values of that type */
    };

So a header is a flat list of tagged values.  That is all.  Everything
structured about an RPM, the file list, the dependencies, the
changelog, is built out of several of those flat values that you are
expected to read in parallel.  Turning that back into something a
person can edit is the job tarpm does.

The files in an extracted tree
==============================

``lead.json``
    The 96 byte lead.  Informational only.

``signature.json``
    The signature header.  Informational only.

``header.json``
    The main header.  This is the one you edit.

``description.txt``, ``postin.txt``, ``spec.txt``, and friends
    The value of a few long text tags, kept out of the JSON so you can
    edit them in peace.  A translated description also gets one
    ``description.<locale>.txt`` per locale.

``payload/``
    The cpio archive, unpacked.

lead.json
=========

::

    {
      "lead magic": "0xEDABEEDB",
      "version": "3.0",
      "type": "binary",
      "name": "bash-5.3.9-3.fc44",
      "architecture": 0,
      "os": 0,
      "signature type": 5,
      "read-only": "true"
    }

The lead is a leftover from RPM 3 and nothing reads it for anything
meaningful any more, beyond the magic number.  tarpm shows it because
it is there, and builds a fresh one out of the Name, Epoch, Version,
Release, and Sourcepackage tags when it creates a package.  Editing
this file changes nothing.  The directory you create a package from
does not need one.

The ``version`` field tracks the RPM format.  It reads ``3.0`` for a
v4 format package and ``4.0`` for a v6 format package.

``architecture`` and ``os`` are zero above because rpm 6.0.0 stopped
filling them in.  An older rpm wrote the numbers of the machine doing
the build rather than anything out of the header, and tarpm does the
same when it was built against one, so a package it writes still
matches the one rpmbuild(1) would have written.

signature.json
==============

Same binary layout as the main header, a different set of tags::

    {
      "magic": "0x8EADE801",
      "reserved": "0000",
      "tags": [
        {
          "tag": "Sha256",
          "type": "string",
          "read-only": "true",
          "value": "c5f4c736fdbc0c821939e63f9caae20b0248a63d8a777922d1a1cca1be0879b3"
        },
        ...
      ]
    }

Every tag in here carries ``"read-only": "true"`` because tarpm builds
the whole signature header itself when creating a package.  The
directory you create from does not need a ``signature.json``, and
tarpm does not read one that is there.  The exceptions are ``-D``,
which takes the Md5, Sha1, Sha256, and Sha3_256 values from it, and
``-k``, which takes the Dsa, Rsa, Openpgp, Pgp, Gpg, and Pgp5 values
from it.  ``-k`` ignores the ``type`` and any other keys in those
entries.

What tarpm writes depends on the format.  A v4 format package gets::

    Headersignatures  Sha1  Sha256  Size  Md5  Payloadsize  Reservedspace

A v4 format package with a header and payload of 4294967295 bytes or
more, or a payload that large uncompressed, gets Longsize and
Longarchivesize in place of Size and Payloadsize.  Reservedspace
shrinks by the same number of bytes, so the signature header keeps its
size.  rpm(8) does the same.

A v6 format package gets::

    Headersignatures  Sha256  Sha3_256  Reserved

``Reservedspace`` is empty padding that rpmsign(1) writes the real
signature into later, which is why a package can be signed without
being rebuilt.

A package built by rpmbuild(1) and then signed carries tags only a
signing key can make, which are Dsa, Rsa, Pgp, Gpg, Pgp5, and Openpgp.
tarpm reads them out so you can see them and writes none of them back
unless ``-k`` is given.  Run rpmsign(1) on the package afterwards.

rpmsign(1) can also sign each file, which puts Filesignatures,
Filesignaturelength, Veritysignatures, and Veritysignaturealgo in the
signature header.  These sign the files and not the package, so they
still hold after a rebuild.  tarpm does not put them in
``signature.json``.  The two per-file lists go to the ``signature`` and
``verity`` keys of the ``files`` array in ``header.json``.
Veritysignaturealgo goes to the ``tags`` array there.
Filesignaturelength is worked out again from the signatures.

header.json
===========

Six keys at the top::

    {
      "magic": "0x8EADE801",
      "reserved": "0000",
      "tags": [ ... ],
      "dependencies": { ... },
      "files": [ ... ],
      "changelog": [ ... ]
    }

``magic`` and ``reserved`` describe the binary header and are not
yours to change.  The other four hold everything else.  ``tags`` is the
flat list of values that do not belong to one of the three structured
groups, and those three groups are the parallel array sets unwound.

tags
----

One dict per header tag.  tarpm refuses a ``null`` or any other value
in ``tags`` that is not a dict and names its index::

    { "tag": "Name",    "type": "string",      "value": "bash" }
    { "tag": "Summary", "type": "i18n string", "value": "The GNU Bourne Again shell" }
    { "tag": "Size",    "type": "int32",       "value": 7843591 }

``tag`` is an ``RPMTAG_`` name from ``/usr/include/rpm/rpmtag.h`` with only
the first letter capitalized, so RPMTAG_BUILDHOST is written
``Buildhost``.  A tag rpm has no name for is written as ``#`` and its
number, such as ``"#5200"``.  tarpm refuses to create a package from a
tag name it does not know.  On create a tag name may be in any case,
so ``PAYLOADCOMPRESSOR`` and ``Payloadcompressor`` are the same tag.
The ``#`` form of a known tag also names that tag.  tarpm warns when
``tags`` carries the same tag more than once.  It writes every copy
but reads only the first, so the defaults and the payload settings
come from that one.  Extract writes the copies in the order the header
has them, so a round trip puts them back the same way.  The signature tag names such as ``Md5``
only work in ``signature.json``.  ``type`` is one of the RPM header
types, spelled out:

================== =====================
JSON type name     rpm type
================== =====================
``char``           RPM_CHAR_TYPE
``int8``           RPM_INT8_TYPE
``int16``          RPM_INT16_TYPE
``int32``          RPM_INT32_TYPE
``int64``          RPM_INT64_TYPE
``string``         RPM_STRING_TYPE
``string array``   RPM_STRING_ARRAY_TYPE
``i18n string``    RPM_I18NSTRING_TYPE
``binary blob``    RPM_BIN_TYPE
================== =====================

An array type carries an array for its ``value``.  A binary blob
carries base64.
A ``string`` or ``i18n string`` tag needs a JSON string for its
``value``.  Write ``"2"`` and not ``2``.  tarpm will not create a
package from a string tag holding a number, a boolean, or ``null``.
An ``i18n string`` tag with translations holds an object instead.  See
`Translated tags`_.

The file list tags do not belong in ``tags``.  tarpm builds them from
the ``files`` array and refuses a ``header.json`` that has them under
``tags``.  To rebuild the file list from the payload tree, remove them.
The three changelog tags are allowed under ``tags`` if they hold the
same number of values.  tarpm refuses them otherwise.

The other types are checked the same way before the header is built.
An empty array is refused for every type, because rpm(8) does not read
a tag with no values.

* A ``string array`` holds an array of JSON strings.  A bare string or
  a ``null`` in the array is refused.
* ``int8``, ``int16``, ``int32``, and ``int64`` hold a number or an
  array of numbers.  A string of digits such as ``"7"`` counts as a
  number.  Booleans, ``null``, and other strings are refused, apart
  from the names and timestamps described below.
* tarpm extracts an ``int16`` value as a number from 0 to 65535.  On
  create a negative ``int16`` value is written as the same 16 bits, so
  -1 becomes 65535.
* A number has to fit in its type.  An ``int8`` takes -128 to 255, an
  ``int16`` -32768 to 65535 and an ``int32`` -2147483648 to
  4294967295.  tarpm refuses a value outside that range rather than
  cut it down.
* A ``binary blob`` holds a base64 string.
* A ``char`` holds a string of one or more characters.  Each character
  is one item in the tag.
* A ``type`` not in the table above is refused.  This includes
  ``(null)`` and ``(unknown)``, which tarpm writes when it extracts a
  tag of type RPM_NULL_TYPE or a type rpm does not define.

A JSON ``null`` for a ``tag`` name, or for the ``path`` or ``type`` of
a ``files`` entry, counts as an empty string.  A ``null`` tag name is an
unknown tag.  A ``files`` entry with a ``null`` path names no file.

The name is the one the rpm tarpm was built against uses, and rpm
renamed three tags in 6.0.0.  A tree extracted with an older rpm calls
them ``Payloaddigest``, ``Payloaddigestalgo``, and ``Payloaddigestalt``
where a newer one says ``Payloadsha256``, ``Payloadsha256algo``, and
``Payloadsha256alt``.  The numbers did not move and tarpm takes either
spelling, so a tree stays usable whichever rpm made it.

Three tags come out differently than the raw header has them, because
the raw form is unreadable.  Note that the ``type`` of the first two
reads ``string`` rather than ``int32`` to match what is written:

* ``Buildtime`` becomes an ISO 8601 string in UTC, such as
  ``"2026-01-16T09:11:11Z"``
* ``Filedigestalgo`` and ``Payloadsha256algo`` become the algorithm
  name, such as ``"sha256"``, and fall back to the raw number for an
  algorithm tarpm has no name for
* ``Headerimmutable`` carries a ``trailer`` dict alongside its value,
  which marks it as the region trailer rather than an ordinary tag.
  The ``tags`` array may hold only one ``Headerimmutable`` entry.
  Its ``type`` must be ``binary blob`` and its value must decode to exactly 16
  bytes, or tarpm refuses to create the package

File backed tags
~~~~~~~~~~~~~~~~

A long text value gets a ``file`` key instead of a ``value`` key::

    { "tag": "Description", "type": "i18n string", "file": "description.txt" }

The file sits next to ``header.json``.  A relative ``file`` key is taken
from the directory holding ``header.json``.  An absolute one is used as
it is.  When creating a package, the file has to end up below the
directory holding ``header.json`` or below the payload directory.  The
payload directory does not have to sit next to ``header.json``.  The
file and the directories leading to it from there may not be symlinks.
Anything else is refused.  The tags that work this way are
Description, the Spec tag of a source package, and every scriptlet,
which is Prein, Postin, Preun, Postun, Pretrans, Posttrans, Preuntrans,
and Postuntrans.  The file is named after the tag in lower case with
``.txt`` on the end.  An empty value gets an empty file.

A header you write by hand can give any of these a plain ``value``
instead and keep the text in the JSON.  tarpm takes either one.

Translated tags
~~~~~~~~~~~~~~~

An ``i18n string`` tag holds one string per locale.  The locales are in
the ``Headeri18ntable`` tag, and the strings follow that order.  A tag
with one string has a plain ``value``.  A tag with more than one has an
object keyed by locale::

    { "tag": "Headeri18ntable", "type": "string array", "value": [ "C", "de" ] }
    { "tag": "Summary", "type": "i18n string",
      "value": { "C": "The GNU Bourne Again shell", "de": "Die GNU Bourne Again Shell" } }

Description keeps each string in a file, so its ``file`` key is the
object.  The first locale uses ``description.txt`` and the others use
``description.<locale>.txt``::

    { "tag": "Description", "type": "i18n string",
      "file": { "C": "description.txt", "de": "description.de.txt" } }

Each file takes the same path checks as any other file backed tag.  A
hand written Description can use ``value`` with the strings in the JSON.

Rules for the object:

* Every locale has to be in ``Headeri18ntable``.  tarpm does not add
  locales to that tag.
* Every value has to be a string.
* A locale with no key gets an empty string if a later locale has a
  key.  rpm(8) shows that locale as blank, the same as for a package
  from rpmbuild(8).  A tag stops at the last locale that has a key, and
  rpm(8) shows the first locale for the ones after that.

tarpm refuses a package with a translated Description when a locale
holds anything other than letters, digits, ``_``, ``-``, ``.``, or
``@``, or starts with ``.``.  The name goes in to a file name.  tarpm
keeps only the first string, with a warning, when a tag has more strings
than ``Headeri18ntable`` has locales.  It does the same when a locale is
in ``Headeri18ntable`` more than once, since the object can hold only
one string for it.

dependencies
------------

RPM stores each kind of dependency as three parallel arrays.  Provides
are Providename, Provideversion, and Provideflags, requires are
Requirename, Requireversion, and Requireflags, and so on through
conflicts, obsoletes, recommends, suggests, supplements, and enhances.
Entry *n* of each array describes dependency *n*.

Any other key under ``dependencies`` is left out of the package with a
warning.

tarpm gathers each of those triples into one array of dicts, keyed by
the dependency type::

    "dependencies": {
      "requires": [
        { "name": "/usr/bin/sh", "sense_flags": [ "find-requires" ] },
        {
          "name": "config(bash)",
          "comparison": "=",
          "version": "5.3.9-3.fc44",
          "sense_flags": [ "config" ]
        },
        { "name": "filesystem", "comparison": ">=", "version": "3" }
      ]
    }

``name`` is the only required key.  ``comparison`` is ``=``, ``<``,
``>``, ``<=``, or ``>=``, and needs a ``version`` to go with it.  That
pair is the comparison bits of the flags word.

``sense_flags`` is the rest of that flags word, as names rather than
bits.  These are the ``RPMSENSE_`` macros in ``/usr/include/rpm/rpmds.h``.
Most of them record where the dependency came from, such as
``find-requires`` for something a generator produced, or what it means,
such as ``config`` or ``rpmlib`` or ``interp``.  A key left off means
the bit is clear.  Bits with no name go in as one decimal number, such
as ``"4194304"``, so a round trip keeps them.

A dependency type with nothing in it does not appear at all, so a
package with no conflicts has no ``conflicts`` key.

``dependencies`` must be a dict and each dependency type in it an
array of dicts.  ``name``, ``comparison``, and ``version`` are strings
and ``sense_flags`` is an array of strings.  tarpm refuses to create a
package when any of them has another JSON type.

files
-----

This is the biggest win of the JSON layout.  RPM spreads the file list
over about two dozen parallel arrays and most of them are numbers or
bitfields::

    Basenames     Dirnames      Dirindexes    Filesizes
    Filemodes     Filemtimes    Fileusername  Filegroupname
    Filerdevs     Filedevices   Filedigests   Filelinktos
    Fileinodes    Fileclass     Classdict     Filelangs
    Filecolors    Fileflags     Fileverifyflags
    Dependsdict   Filedependsn  Filedependsx
    Filemimeindex Mimedict

tarpm reads all of those together and writes one dict per file::

    {
      "path": "/etc/skel/.bash_logout",
      "size": 18,
      "mode": "0644",
      "type": "file",
      "mtime": "2026-01-16T00:00:00Z",
      "user": "root",
      "group": "root",
      "device": 1,
      "digest": "2584c4ba8b0d2a52d94023f420b7e356a1b1a3f2291ad5eba06683d58c48570d",
      "inode": 1,
      "class": "ASCII text",
      "flags": [ "config", "noreplace" ],
      "verifyflags": [
        "filedigest", "filesize", "linkto", "user",
        "group", "mtime", "mode", "rdev", "caps"
      ]
    }

A key is left off when the file has nothing to say for it, so most
entries carry only some of these.  ``path`` is the only one every entry
has.

How each key maps back:

================= =======================================================
key               header tags
================= =======================================================
``path``          Dirnames plus Basenames, joined through Dirindexes
``size``          Filesizes, or Longfilesizes in a v6 format package
                  and in a v4 format package with a file of
                  4294967295 bytes or more.  The key is the same for
                  both tags and tarpm picks the tag when it creates
                  the package.
``mode``          Filemodes, the permission bits only, as octal
``type``          Filemodes, the file type bits, as a name
``mtime``         Filemtimes, as ISO 8601 UTC
``user``          Fileusername
``group``         Filegroupname
``rdev``          Filerdevs, device nodes only, 0 to 65535
``device``        Filedevices
``digest``        Filedigests, in whatever Filedigestalgo names
``linkto``        Filelinktos, symlinks only
``inode``         Fileinodes
``class``         Classdict, indexed through Fileclass
``mime``          Mimedict, indexed through Filemimeindex
``langs``         Filelangs, split on the bar character
``colors``        Filecolors, as Elf32 or Elf64
``flags``         Fileflags, as names
``verifyflags``   Fileverifyflags, as names, an empty array for none
``provides``      Dependsdict, through Filedependsx and Filedependsn
``signature``     Filesignatures in the signature header
``verity``        Veritysignatures in the signature header
================= =======================================================

Six of those are worth a few more words.

``path``
    RPM does not store whole paths.  It stores the unique directories in
    Dirnames, the file names in Basenames, and one index per file into
    the directory list in Dirindexes.  tarpm joins them for you and
    splits them apart again when it writes the header.  A source package
    has no directories, so those paths have no leading slash.

    When creating a package, tarpm finds each file by joining its path to
    the payload directory.  A path with an empty, ``.``, or ``..``
    component is refused.  So is a path that goes through a symlinked
    directory in the payload tree.  The file itself may be a symlink.
    Either one stops the create, since the path would reach a file
    outside the payload tree.  tarpm opens each regular file without
    following a symlink anywhere in its path, so a symlink put in the
    tree while tarpm runs is refused too.

``inode``
    Not a real inode number.  RPM hands these out itself starting at 1,
    and two files sharing a number are hard links of each other.  That
    is the only thing this key means.

    A file with no ``inode`` key is a hard link of the files it is
    linked to in the payload tree.  When one of those files has an
    ``inode`` key, the new file joins its group.  Two files with
    different ``inode`` keys are never hard links of each other, even
    when they are linked in the payload tree.

``class``
    Dictionary encoded.  Classdict holds each distinct class string once
    and Fileclass holds one index per file.  tarpm builds both when
    writing.  A file with no class has no key, which is common and
    normal.

    A v6 format package has none of this.  It records a MIME type per
    file instead, so a file list from a v6 format package carries no
    ``class`` key at all.

``mime``
    Dictionary encoded the same way.  Mimedict holds each distinct MIME
    type once and Filemimeindex holds one index per file.  tarpm builds
    both when writing a v6 format binary package.  Every file has a
    MIME type, so nearly every entry from a v6 format package carries
    this key.  A v4 format package has no MIME types.

    Switching formats fills in what the new format needs.  Writing a v6
    format package works out the MIME type of an entry with no ``mime``
    key.  Writing a v4 format package works out the class of an entry
    with a ``mime`` key and no ``class`` key.

``provides``
    Despite the name, both provides and requires show up here, because
    RPM records a dependency against the file the generator read it out
    of.  Each entry looks like a dependency from the ``dependencies``
    dict with a ``type`` key added::

        "provides": [
          {
            "type": "requires",
            "name": "/usr/bin/sh",
            "sense_flags": [ "find-requires" ]
          }
        ]

    The order matters, since RPM points at a run of these per file, and
    tarpm keeps it.

``signature``, ``verity``
    The IMA file signature as hex and the fs-verity signature as
    base64, from rpmsign(1).  RPM keeps these in the signature header
    with one entry per file and an empty string for a file with none.
    A file with none has no key.  The fs-verity hash algorithm is the
    Veritysignaturealgo tag in ``tags``.  It is an integer, 1 for sha256
    and 2 for sha512.  Without it tarpm writes 1, the same as rpmsign(1).
    A value that is not a number gets a warning and tarpm writes 1.

    tarpm writes them to the signature header in both formats.  The
    room they take comes out of the reserved space, so the signature
    header keeps its size, the same as rpmsign(1) does.  Signatures too
    large for the reserved space leave it alone and the signature
    header grows.  A file whose contents changed loses both, since they
    sign the old contents.  A source package never gets them.

    An older rpmsign(1) put the file signatures in the main header of a
    v4 format package.  Extracting one of those moves them to the
    ``signature`` keys as well, and drops Filesignatures and
    Filesignaturelength from ``tags``.  A package tarpm creates from it
    has them in the signature header, in either format.

    rpm(8) turns away a package with file signatures in both headers.
    If you put a Filesignatures tag in ``tags`` yourself, a v4 format
    package keeps it and tarpm leaves the ``signature`` keys out of the
    signature header.

changelog
---------

Three parallel arrays again, Changelogtime, Changelogname, and
Changelogtext, one entry each per changelog block::

    "changelog": [
      {
        "timestamp": "Fri Jan 16 2026",
        "name": "Fedora Release Engineering <releng@fedoraproject.org> - 5.3.9-3",
        "text": [
          "- Rebuilt for https://fedoraproject.org/wiki/Fedora_44_Mass_Rebuild"
        ]
      }
    ]

``timestamp`` is a time_t in the header and the spec file date format
here.  A time other than noon UTC is written with the time of day, such
as ``"Thu Oct 06 06:48:39 UTC 2016"``.  ``name`` is everything after
the date on the ``%changelog`` line, which by convention is the
packager and the version-release of that update, but RPM does not care
what is in it.  ``text`` is one
string in the header and tarpm splits it on newlines so diffs are
readable.  A blank line is an empty string in the array.  The array is
joined back together with newlines when the package is written, so
the text comes back exactly as it was.  Every line in ``text`` must be
a JSON string.

Newest entry first, which is the order rpm(8) expects.

An entry with no ``name`` or no ``text`` is left out with a warning.  If
no entry is left, the package carries no changelog tags.  An entry with
no ``timestamp``, or one tarpm cannot read, stops the create.

Read-only markers
=================

Anything with ``"read-only": "true"`` is informational.  tarpm computes
it fresh when creating a package and ignores whatever you left there.
That covers all of ``lead.json`` and every tag in ``signature.json``.

``header.json`` carries no read-only markers, since it is the file you
are meant to edit, but three things in it are still not yours to
change.  Those are the ``magic`` key, the ``reserved`` key, and the
``Headerimmutable`` tag holding the region trailer.

The digest and size tags in the main header are not marked read-only
but are recomputed anyway, because they describe bytes tarpm is writing
and cannot be left stale.  See :doc:`modifying`.
