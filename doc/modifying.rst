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
because tarpm builds both of those itself.

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
  and the reserved space for a signature
* the file digests in Filedigests
* the payload digest tags in the main header, which are the
  Payloadsha256 family
* Size and Longsize, Filesizes and Longfilesizes
* the payload sizes, which sit in the signature header for a format 4
  package and in Payloadsize and Payloadsizealt for a format 6 one
* Basenames, Dirnames, and Dirindexes, rebuilt from the ``path`` of
  each file list entry
* Classdict and Fileclass, and Dependsdict with Filedependsn and
  Filedependsx
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
    The ``type``, ``size``, ``linkto``, ``rdev``, ``digest``, and
    ``class`` are taken from the file on disk.  Swapping a file for a
    symlink rewrites the rest of the entry to suit the new type.

Keys you left out
    An entry missing a key the header needs gets one, read off the file
    in the payload tree.  The owner, the group, and the device are not
    things a file can answer, so those fall back to root, root, and 1.

tarpm prints a line on standard error for each of these, which is how
you find out your edit did something you did not mean.

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

Converting between formats
==========================

``-F`` on create writes the package as RPM format 4 or format 6::

    $ tarpm -xf oldpackage.rpm -O tree
    $ tarpm -cf newpackage.rpm -F 6 tree

tarpm drops the main header tags that belong to the other format and
writes the ones the new format wants, so you do not have to edit
``header.json`` first.  The file sizes survive the move, since the two
formats only disagree on which tag they are counted in.  So does
``Sourcenevr``, which only a format 6 package carries but which tarpm
works out again from ``Sourcerpm`` on the way back.

Two things do not survive.  A format 4 package describes each file with
a Fileclass and a format 6 package uses a MIME type, and neither one
can be turned into the other, so the new package goes without.  The
``rpmlib()`` entries in ``dependencies`` are left alone as well, so a
package converted to format 6 keeps the ones rpm(8) only adds for
format 4.  Edit ``dependencies`` yourself if you do not want them.

Signing
=======

A package tarpm writes is unsigned.  It carries the reserved space in
the signature header that a signature goes into, so::

    $ rpmsign --addsign newbash.rpm

Any signature the original package carried is gone, which is correct.
Changing a single byte of the payload invalidates it.
