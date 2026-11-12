=======================
Modifying and repacking
=======================

The loop is extract, edit, create::

    $ tarpm -xf bash-5.3.9-3.fc44.x86_64.rpm -O tree
    $ vim tree/header.json
    $ vim tree/payload/etc/skel/.bashrc
    $ tarpm -cf newbash.rpm tree

``-c`` takes the directory as a positional argument.  It needs a
``header.json`` and a ``payload`` subdirectory and nothing else.  The
``lead.json`` and ``signature.json`` sitting in the tree are ignored,
because tarpm builds both of those itself.  The exceptions are
``-D``, which reads the digests in ``signature.json``, and ``-k``,
which reads the package signatures in it (see below).

A worked example
================

Bump the release, add a file, and drop one::

    $ tarpm -xf bash-5.3.9-3.fc44.x86_64.rpm -O tree
    $ sed -i 's/"3.fc44"/"3.fc44.local"/' tree/header.json
    $ echo hello > tree/payload/etc/skel/.extra
    $ rm -f tree/payload/usr/bin/alias
    $ tarpm -cf newbash.rpm tree
    tarpm: *** /etc/skel/.extra is new in the payload, adding it to the file list
    tarpm: *** /usr/bin/alias is not in the payload, leaving it out of the file list

Check the result::

    $ rpm -qp --qf '%{name}-%{version}-%{release}.%{arch}\n' newbash.rpm
    bash-5.3.9-3.fc44.local.x86_64
    $ rpm --define '_pkgverify_level digest' -K newbash.rpm
    newbash.rpm: digests OK

The digest check passes because tarpm recomputed everything the package
claims about itself.  Use ``_pkgverify_level digest`` when you check a
package tarpm wrote, since there is no signature on it for rpm(8) to
check.

What tarpm recomputes for you
=============================

Never edit these.  They are rebuilt from the bytes tarpm is actually
writing, so whatever you type gets thrown away:

* the lead
* the whole signature header, including the header and payload digests
  and the reserved space for a signature, unless ``-D`` keeps the
  digests in ``signature.json`` or ``-k`` keeps the package signatures
  (see below)
* the file digests in Filedigests, unless ``-D`` keeps the ones in
  ``header.json``
* the payload digest tags in the main header, which are the
  Payloadsha256 family, unless ``-D`` keeps the ones in
  ``header.json``
* Size and Longsize, Filesizes and Longfilesizes.  A header with a
  ``files`` array and neither Size nor Longsize gets the one the total
  fits in.
* the payload sizes, which sit in the signature header for a v4 format
  package and in Payloadsize and Payloadsizealt for a v6 format one
* Basenames, Dirnames, and Dirindexes, rebuilt from the ``path`` of
  each file list entry
* Classdict and Fileclass, Mimedict and Filemimeindex, and Dependsdict
  with Filedependsn and Filedependsx
* the Headerimmutable region trailer

What you control
================

Everything in ``tags``, ``dependencies``, and ``changelog`` goes into
the header as you wrote it.  tarpm checks your types and nothing else.
There is no spec file to validate against, so a bad Requires or a
Version with a hyphen in it goes straight through and rpm(8) is the one
that complains.

In ``files``, these come from the JSON and not from the file on disk:

* ``mode``
* ``mtime``
* ``user`` and ``group``
* ``flags`` and ``verifyflags``
* ``langs`` and ``colors``

So running ``chmod`` or ``touch`` on a file in the payload tree does
**not** change the package.  The entry already says what the mode and
the timestamp should be.  Edit the ``mode`` and ``mtime`` keys in
``header.json`` instead.  This is on purpose, since extraction cannot
preserve ownership and you do not want your umask leaking into a
package.

How the payload tree and the file list get reconciled
=====================================================

tarpm walks both and sorts out the differences.

Files you removed
    An entry naming a file that is not in the payload tree is left out
    of the new package.  A ``%ghost`` is never in the payload to start
    with, so ghosts are always kept.

Files you added
    A file in the payload tree with no entry gets one, filled in from
    the file itself.  Everything about it, including its mode and
    timestamp, is read off the file, because there is no entry saying
    otherwise.  The directories leading down to a file the list already
    names do not get entries of their own, since the payload tree needs
    them to hold the files but the package never owned them.

Files you changed
    The ``type``, ``size``, ``linkto``, ``rdev``, ``digest``,
    ``class``, and ``mime`` are taken from the file on disk.  Swapping a file for a
    symlink rewrites the rest of the entry to suit the new type.  A
    file whose contents changed loses its ``signature`` and ``verity``
    keys.

Keys you left out
    An entry missing a key the header needs gets one, read off the file
    in the payload tree.  The owner, the group, and the device are not
    things a file can answer, so those fall back to root, root, and 1.

tarpm warns on standard error when it adds or drops an entry, when a
file changed type, when it fills in a missing key, and when a changed
file loses its signatures.  That is how you find out your edit did
something you did not mean.  A file whose size or contents changed
gets no warning.  ``-v`` names those as well, and it reports added and
dropped files in its status lines in place of the warnings.  See
:doc:`creating`.

Taking values off the payload tree instead
==========================================

Sometimes the JSON is the wrong source.  ``-m``, ``-u``, ``-g``, and
``-l`` turn the rule around for the timestamp, the owner, the group,
and the symlink target::

    $ tarpm -cf new.rpm -m tree

With ``-m``, the mtime of each file in the payload tree is what lands
in the header and the ``mtime`` key is disregarded.  ``-a`` is all four
at once.  They combine freely, so you can take the owner off the tree
without taking the group off it.  An entry with no file in the payload
tree, such as a ghost, is left as ``header.json`` has it.

Keep in mind that ``-u`` and ``-g`` take the owner of the extracted
file, which is you, not root.  That is usually not what you want.

Keeping the digests
===================

``-D`` tells tarpm to use the digests in ``header.json`` and
``signature.json`` as they stand instead of recomputing them::

    $ tarpm -cf bad.rpm -D tree

This covers every digest tarpm computes when creating a package:

* the ``digest`` of each file list entry, which goes in to Filedigests
* the payload digest tags in ``header.json``, which are the
  Payloadsha256 family
* the Md5, Sha1, Sha256, and Sha3_256 tags in ``signature.json``

The values are not checked.  An edited digest reaches the package even
though it no longer matches what it covers.  The signature digests in
``signature.json`` must have the right length.  That is 16 bytes in
base64 for Md5, 40 hex digits for Sha1, and 64 for Sha256 and Sha3_256.
tarpm warns about one that does not and computes it instead.  This is how you build a
package with known bad digests to test a tool that verifies them.

A digest that is missing still gets one computed.  That goes for a file
list entry with no ``digest``, a payload digest tag that is not in
``header.json``, and a signature digest that is not in
``signature.json``.  A tree with no ``signature.json`` gets every
signature digest computed.  ``-S`` names a ``signature.json`` outside
the tree.  If the file ``-S`` names cannot be read, tarpm exits
non-zero.

A signature digest is only kept when the format puts that tag in the
signature header.  A v6 format package never gets the Md5 or Sha1 tags
from a v4 format ``signature.json``.  The sizes and the reserved space
are still computed.

Keep in mind that the header digests in ``signature.json`` cover the
main header.  Any edit to ``header.json`` makes the kept ones wrong,
and rpm(8) then refuses to query the package.  Remove
``signature.json`` from the tree to keep only the digests in
``header.json``.

Keeping the package signatures
==============================

A package tarpm creates carries no package signatures by default.
``-k`` copies the ones in ``signature.json`` in to the new package::

    $ tarpm -cf resigned.rpm -k tree

These are the Dsa, Rsa, Openpgp, Pgp, Gpg, and Pgp5 tags.  A tag with
no value is left out.  If a tag appears more than once, tarpm warns and
keeps the first.  rpm(8) does not read Pgp, Gpg, and Pgp5 in a v6 format
package, so tarpm warns and leaves them out of one.  All six go in a v4
format package.  tarpm copies only the value and gives each tag
the type rpm uses for it.  Openpgp must be an array of non-empty strings.
The others must be base64 strings.  tarpm refuses any other value.  The
signatures themselves are not checked.  The room they take
comes out of the reserved space in the signature header, the same as
rpmsign(1), so the package does not grow unless they overflow it.

A package signature covers the main header, and Pgp and Gpg cover the
payload too.  tarpm rebuilds both, so a kept signature only verifies
when they come out byte for byte the same as in the signed package.
Otherwise rpm(8) reports the signature as BAD.  tarpm warns when the
Sha256 of the new main header differs from the one in
``signature.json``.  It does not check the payload that Pgp and Gpg
also cover.  This is how you build a package with a known bad signature
to test a tool that verifies them.

``-k`` does not keep the signature digests.  Add ``-D`` for that.  A
tree with no ``signature.json`` gets no package signatures.  ``-S``
names a ``signature.json`` outside the tree.  The file and verity
signatures come from ``header.json`` with or without ``-k``.

Converting between formats
==========================

``-F`` on create writes the package in the v4 format or the v6
format::

    $ tarpm -xf oldpackage.rpm -O tree
    $ tarpm -cf newpackage.rpm -F 6 tree

Without ``-F`` the package is written in the v4 format, even when the
tree came out of a v6 format package.  Pass ``-F 6`` to keep a v6
format package in the v6 format.

tarpm drops the main header tags that belong to the other format and
writes the ones the new format wants, so you do not have to edit
``header.json`` first.  The file sizes survive the move, since the two
formats only disagree on which tag they are counted in.  So does
``Sourcenevr``, which only a v6 format package carries but which tarpm
works out again from ``Sourcerpm`` on the way back.

A v4 format package describes each file with a class and a v6 format
package uses a MIME type.  tarpm works out the one the new format needs
from the file in the payload tree.

One thing does not survive.  The ``rpmlib()`` entries in
``dependencies`` are left alone, so a package converted to v6 format
keeps the ones rpm(8) only adds for v4 format.  Edit ``dependencies``
yourself if you do not want them.

Signing
=======

A package tarpm writes is unsigned unless ``-k`` kept a signature.  It
carries the reserved space in the signature header that a signature
goes into, so::

    $ rpmsign --addsign newbash.rpm

Any signature the original package carried is gone, which is correct.
Changing a single byte of the payload invalidates it.

The file signatures in the ``files`` array are different.  They sign
one file each, so tarpm keeps them for the files that did not change.
Sign the
files again with ``rpmsign --signfiles`` or ``rpmsign --signverity``.
