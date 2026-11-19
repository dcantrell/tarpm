========
Overview
========

What tarpm does
===============

An RPM file is usually at least four data structures.  There is a 96
byte lead, a signature header, a main header, and a compressed cpio
payload.  The two headers are binary structures holding tagged values
and the payload holds the files.

tarpm reads all four of those and writes them back out:

* ``tarpm -tf`` lists the payload the way ``tar -tf`` does
* ``tarpm -xf`` extracts the package to a directory
* ``tarpm -cf`` creates a package from a directory

When you extract, the payload becomes a normal directory tree and the
headers become JSON files you can open in an editor.  When you create,
tarpm reads that JSON back, walks the payload tree, recomputes
everything that has to be recomputed, and writes a new package.

rpm writes the cpio archive two different ways and tarpm reads both of
them.  That matters when one of them fails to unpack.  See
:doc:`extracting`.

Since the interface mimics tar(1), you can run the options together
the way you would with tar.  ``tarpm -xvf`` and ``tarpm -cvf`` both
work, and the leading hyphen on a combined set like that is optional::

    $ tarpm xvf bash-5.3.9-3.fc44.x86_64.rpm

Without the hyphen the combined options have to come first on the
command line.  Non-tar-like options still need to specified
separately.

How this differs from rpmbuild
==============================

rpmbuild builds software.  You give it a spec file and some sources
and it runs ``%prep``, ``%build``, and ``%install``, then packages up
whatever landed in the buildroot.  The header falls out of that work as
a side effect.

tarpm does not build anything.  It edits the end product instead,
either source or binary RPM files.  There is no spec file, no macro
expansion, no buildroot, and no build scriptlets.  What you put in
``header.json`` is what you get in the header.

Primary differences:

no spec file
   tarpm never reads one.  A source package carries its spec as payload
   data like any other file and tarpm treats it that way.

no macros
   ``%{name}`` in a JSON value is the literal string ``%{name}``.
   Nothing expands it.

no automatic dependencies
   rpmbuild runs the dependency generators for you.  tarpm writes the
   ``dependencies`` dict exactly as you leave it.  You can still run
   ``/usr/lib/rpm/find-requires`` and ``/usr/lib/rpm/find-provides``
   yourself and either modify or construct a ``dependencies`` dict if
   you want to.  See :doc:`creating`.

no ``%files`` globbing
   The payload tree is the file list.  A file in the tree is in the
   package and a file you delete from the tree is not.

no validation
   tarpm checks that your JSON types are right and very little else.
   It will happily write a header that rpm(8) then refuses to install.
   rpmbuild would have caught that for you.  On the other hand, tarpm
   can be used to create known bad RPM files for testing purposes.

What tarpm does take care of is the mechanical part.  The file digests,
the payload digests, the sizes, the cpio archive, the compression, the
lead, the signature header, and the header region trailer all get built
fresh every time you create a package.  You cannot get any of those
wrong by hand because you never write them by hand.  If you want them
wrong on purpose, ``-D`` keeps the digests you already have.

What it does not do
===================

tarpm will not sign a package.  Signing needs a private key, so run
rpmsign(1) on the package after tarpm writes it.  Any signature the
original package carried gets dropped unless you pass ``-k``.  This is
deliberate since changing anything in the package technically
invalidates any package signature.  The file signatures are the
exception here.  tarpm keeps those for the files that did not change.

tarpm also does not split debuginfo, strip binaries, compress man
pages, or do any of the other things rpmbuild does to a buildroot on
your behalf.  The payload tree goes in to the package the way you left
it.

Use cases
=========

Looking at a package
   ``tarpm -xnf`` gets you the whole header as readable JSON without
   unpacking the payload.  This beats a pile of ``rpm -qp --qf``
   invocations when you want to see everything at once, and it makes
   two packages much easier to diff.

Patching a package you cannot rebuild
   A vendor binary, something from an old release, or anything else
   where the sources are long gone.  Extract it, change the file, and
   repack it.

Converting between RPM formats
   Run ``tarpm -xf`` on a v4 format package and then ``tarpm -cf -F 6``
   to write it back out.  tarpm drops the tags belonging to the other
   format and writes the ones the new format requires.  See
   :doc:`modifying`.

Scripted package surgery
   The JSON structure is meant for programs to read.  Load
   ``header.json``, change a tag, write it back, and create the
   package.  No spec file templating and no parsing of ``rpm -qp``
   output.

Building a package with no spec file
   A directory of files plus five tags in ``header.json`` is enough to
   make an installable package.  This is useful in CI, or for shipping
   a tree of artifacts that was never going to have a spec file.

Test packages
   Packages with deliberately strange headers are hard to produce with
   rpmbuild and easy to produce with tarpm.  If you are testing a tool
   that reads RPMs, this is how you feed it bad input on purpose.
