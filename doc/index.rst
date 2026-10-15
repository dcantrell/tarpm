=====
tarpm
=====

tarpm is a command line tool for taking binary and source RPM packages
apart and putting them back together.  It works the way tar(1) works.
You extract a package to a directory, you change what is in that
directory, and you create a new package from it.

The RPM header comes out as JSON files that you can read and edit.  The
payload comes out as a plain directory tree.  No spec file is involved
at any point.

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
    What you get, where it goes, and how to read it.

:doc:`metadata`
    The reference.  Every JSON file and how each piece maps back to
    the binary RPM header.

:doc:`modifying`
    Changing a package and writing it back out, including what tarpm
    recomputes and what it leaves to you.

:doc:`creating`
    Binary packages in format 4 and format 6, source packages, and
    dependencies.

The tarpm(1) man page is the reference for every command line option.
These documents explain how the pieces fit together and what to do with
them.
