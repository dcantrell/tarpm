========
Overview
========

What tarpm does
===============

An RPM file is four things stuck together.  A 96 byte lead, a signature
header, a main header, and a compressed cpio payload.  The two headers
are binary structures holding tagged values.  The payload holds the
files.

tarpm reads all four and writes them back out:

* ``tarpm -tf`` lists the payload, the way ``tar -tf`` does
* ``tarpm -xf`` extracts the package to a directory
* ``tarpm -cf`` creates a package from a directory

When you extract, the payload becomes a normal directory tree and the
headers become JSON files you can open in an editor.  When you create,
tarpm reads the JSON back, walks the payload tree, recomputes
everything that has to be recomputed, and writes a new package.

rpm writes that cpio archive two different ways and tarpm reads both,
which matters when one fails to unpack.  See :doc:`extracting`.

How this differs from rpmbuild
==============================

rpmbuild builds software.  You give it a spec file and some sources and
it runs ``%prep``, ``%build``, and ``%install``, then packages up what
landed in the buildroot.  The header is a side effect of that work.

tarpm does not build anything.  It edits the end product.  There is no
spec file, no macro expansion, no buildroot, and no build scriptlets.
What you put in ``header.json`` is what goes in the header.

The practical differences:

no spec file
   tarpm never reads one.  A source package carries its spec as payload
   data like any other file, and tarpm treats it that way.

no macros
   ``%{name}`` in a JSON value is the literal string ``%{name}``.
   Nothing expands it.

no automatic dependencies
   rpmbuild runs the dependency generators for you.  tarpm writes the
   ``dependencies`` dict exactly as you leave it.  You can run
   ``/usr/lib/rpm/find-requires`` and ``/usr/lib/rpm/find-provides``
   yourself.  See :doc:`creating`.

no ``%files`` globbing
   The payload tree is the file list.  A file in the tree is in the
   package and a file you delete from the tree is not.

no validation
   tarpm checks that your JSON types are right and little else.  It will
   happily write a header that rpm(8) refuses to install.  rpmbuild
   would have caught that for you.

What tarpm does take care of is the mechanical part.  File digests, the
payload digests, sizes, the cpio archive, the compression, the lead, the
signature header, and the header region trailer are all built fresh
every time you create a package.  You cannot get those wrong by hand
because you do not write them by hand.

What it does not do
===================

tarpm will not sign a package.  Signing needs a private key, so run
rpmsign(1) on the package after tarpm writes it.  Any signature the
original package carried is dropped, since changing anything in the
package invalidates it anyway.

tarpm also does not split debuginfo, strip binaries, compress man pages,
or do any of the other things rpmbuild does to a buildroot on your
behalf.  The payload tree goes in as it sits.

Use cases
=========

Looking at a package
   ``tarpm -xnf`` gets you the whole header as readable JSON without
   unpacking the payload.  This beats a pile of ``rpm -qp --qf``
   invocations when you want to see everything at once, and it is
   easier to diff two packages this way.

Patching a package you cannot rebuild
   A vendor binary, something from an old release, anything where the
   sources are gone.  Extract it, change the file, repack it.

Converting between RPM formats
   ``tarpm -xf`` a v4 package and ``tarpm -cf -F 6`` it back out.  tarpm
   drops the tags that belong to the other format and writes the ones
   the new format wants.  See :doc:`modifying`.

Scripted package surgery
   The JSON structure is meant for programs.  Load ``header.json``,
   change a tag, write it back, create the package.  No spec file
   templating and no parsing of ``rpm -qp`` output.

Building a package with no spec file
   A directory of files plus five tags in ``header.json`` makes an
   installable package.  Useful in CI, or for shipping a tree of
   artifacts that was never going to have a spec file.

Test fixtures
   Packages with deliberately strange headers are hard to produce with
   rpmbuild and easy to produce with tarpm.  If you are testing a tool
   that reads RPMs, this is a way to feed it bad input on purpose.
