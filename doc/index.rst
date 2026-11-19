=====
tarpm
=====

tarpm is a command line tool for taking binary and source RPM packages
apart and putting them back together.  You can also create installable
RPM packages by providing metadata for the header and a payload tree.
The interface mimics tar(1), so if you know how to drive tar you
already know how to drive tarpm.  You extract a package to a
directory, you change what you find in that directory, and then you
create a new package from it.

When you extract a package, the RPM header is written to a set of JSON
files you can read and edit with whatever tool you like.  The payload
is extracted to a regular directory tree.  No spec file is involved at
any point and tarpm never calls rpmbuild(1).

.. toctree::
   :maxdepth: 2
   :caption: Contents

   overview
   building
   listing
   extracting
   metadata
   modifying
   creating

:doc:`overview`
    What tarpm does, how it differs from rpmbuild(1), and what it is
    good for.

:doc:`building`
    Requirements, building the tree, running the test suite, and
    building these documents.

:doc:`listing`
    Reading a package without unpacking it.

:doc:`extracting`
    What you get when you extract a package, where it all goes, and
    how to read it.

:doc:`metadata`
    The reference.  Every JSON file and how each piece maps back to
    the binary RPM header.

:doc:`modifying`
    Changing a package and writing it back out, including what tarpm
    recomputes for you and what it leaves alone.

:doc:`creating`
    Binary packages in the v4 format and the v6 format, source
    packages, and dependencies.

The tarpm(1) man page is the reference for every command line option.
These documents explain how the pieces fit together and what you can do
with them.
