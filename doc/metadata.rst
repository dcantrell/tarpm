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
    edit them in peace.

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
format 4 package and ``4.0`` for a format 6 package.

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
tarpm does not read one that is there.

What tarpm writes depends on the format.  A format 4 package gets::

    Headersignatures  Sha1  Sha256  Size  Md5  Payloadsize  Reservedspace

A format 6 package gets::

    Headersignatures  Sha256  Sha3_256  Reserved

``Reservedspace`` is empty padding that rpmsign(1) writes the real
signature into later, which is why a package can be signed without
being rebuilt.

A package built by rpmbuild(1) and then signed carries tags only a
signing key can make, which are Dsa, Rsa, Pgp, Gpg, Pgp5, Openpgp,
Filesignatures, Filesignaturelength, Veritysignatures, and
Veritysignaturealgo.  tarpm reads them out so you can see them and
writes none of them back.  Run rpmsign(1) on the package afterwards.

header.json
===========

Five keys at the top::

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

One dict per header tag::

    { "tag": "Name",    "type": "string",      "value": "bash" }
    { "tag": "Summary", "type": "i18n string", "value": "The GNU Bourne Again shell" }
    { "tag": "Size",    "type": "int32",       "value": 7843591 }

``tag`` is an ``RPMTAG_`` name from ``/usr/include/rpm/rpmtag.h`` with only
the first letter capitalized, so RPMTAG_BUILDHOST is written
``Buildhost``.  ``type`` is one of the RPM header types, spelled out:

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

Three tags come out differently than the raw header has them, because
the raw form is unreadable.  Note that the ``type`` of the first two
reads ``string`` rather than ``int32`` to match what is written:

* ``Buildtime`` becomes an ISO 8601 string in UTC, such as
  ``"2026-01-16T09:11:11Z"``
* ``Filedigestalgo`` and ``Payloadsha256algo`` become the algorithm
  name, such as ``"sha256"``, and fall back to the raw number for an
  algorithm tarpm has no name for
* ``Headerimmutable`` carries a ``trailer`` dict alongside its value,
  which marks it as the region trailer rather than an ordinary tag

File backed tags
~~~~~~~~~~~~~~~~

A long text value gets a ``file`` key instead of a ``value`` key::

    { "tag": "Description", "type": "i18n string", "file": "description.txt" }

The file sits next to ``header.json``.  The tags that work this way are
Description, the Spec tag of a source package, and every scriptlet,
which is Prein, Postin, Preun, Postun, Pretrans, Posttrans, Preuntrans,
and Postuntrans.  The file is named after the tag in lower case with
``.txt`` on the end.

A header you write by hand can give any of these a plain ``value``
instead and keep the text in the JSON.  tarpm takes either one.

dependencies
------------

RPM stores each kind of dependency as three parallel arrays.  Provides
are Providename, Provideversion, and Provideflags, requires are
Requirename, Requireversion, and Requireflags, and so on through
conflicts, obsoletes, recommends, suggests, supplements, and enhances.
Entry *n* of each array describes dependency *n*.

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
the bit is clear.

A dependency type with nothing in it does not appear at all, so a
package with no conflicts has no ``conflicts`` key.

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
``size``          Filesizes, or Longfilesizes in a format 6 package
``mode``          Filemodes, the permission bits only, as octal
``type``          Filemodes, the file type bits, as a name
``mtime``         Filemtimes, as ISO 8601 UTC
``user``          Fileusername
``group``         Filegroupname
``rdev``          Filerdevs, device nodes only
``device``        Filedevices
``digest``        Filedigests, in whatever Filedigestalgo names
``linkto``        Filelinktos, symlinks only
``inode``         Fileinodes
``class``         Classdict, indexed through Fileclass
``langs``         Filelangs, split on the bar character
``colors``        Filecolors, as Elf32 or Elf64
``flags``         Fileflags, as names
``verifyflags``   Fileverifyflags, as names
``provides``      Dependsdict, through Filedependsx and Filedependsn
================= =======================================================

Four of those are worth a few more words.

``path``
    RPM does not store whole paths.  It stores the unique directories in
    Dirnames, the file names in Basenames, and one index per file into
    the directory list in Dirindexes.  tarpm joins them for you and
    splits them apart again when it writes the header.  A source package
    has no directories, so those paths have no leading slash.

``inode``
    Not a real inode number.  RPM hands these out itself starting at 1,
    and two files sharing a number are hard links of each other.  That
    is the only thing this key means.

``class``
    Dictionary encoded.  Classdict holds each distinct class string once
    and Fileclass holds one index per file.  tarpm builds both when
    writing.  A file with no class has no key, which is common and
    normal.

    A format 6 package has none of this.  It records a MIME type per
    file in Mimedict and Filemimeindex instead, and tarpm leaves those
    two in the ``tags`` array rather than folding them in to the file
    entries.  So a file list from a format 6 package carries no
    ``class`` key at all, and editing the MIME type means editing the
    two raw tags and keeping the indexes straight yourself.

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
here.  ``name`` is everything after the date on the ``%changelog``
line, which by convention is the packager and the version-release of
that update, but RPM does not care what is in it.  ``text`` is one
string in the header and tarpm splits it on newlines so diffs are
readable.  The array is joined back together with newlines when the
package is written.

Newest entry first, which is the order rpm(8) expects.

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
