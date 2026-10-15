========
Building
========

Requirements
============

tarpm is C and builds with meson and ninja.  The libraries it needs are:

* librpm, for the tag tables and a few helpers
* json-c, for reading and writing the JSON metadata
* libarchive, for the cpio payload
* libmagic, for working out file classes
* libzstd, for v6 payload compression
* OpenSSL, for the digests

The test suite also wants CUnit, python3, python3-rpm, and rpm-build.
The HTML manual wants doxygen, sphinx, breathe, and the Read the Docs
sphinx theme.

On Fedora the whole list is in ``osdeps/fedora/reqs.txt`` and this
installs it::

    make instreqs

Building
========

The tree uses meson, but there is a GNUmakefile wrapping the common
steps so you do not have to remember them::

    make

That runs ``meson setup build`` and then ``ninja -C build``.  The
binary lands at ``build/src/tarpm`` and runs out of the build directory
without installing anything.

For a debug build with warnings as errors::

    make debug

To start over::

    make clean

Documentation
=============

These documents and the man page build along with everything else, so
a plain ``make`` writes the HTML manual to ``build/doc/html``.  To
build only that::

    make docs

doxygen turns the header files in ``include/`` in to XML and sphinx
reads that through breathe alongside the rst files in ``doc/``.  A
missing piece skips the manual with a warning rather than failing the
build of the program, so you can build tarpm without any of it.  Turn
it off outright with::

    meson setup build -Ddocs=false

The other meson options are ``-Dnls=false`` to skip translations and
``-Dtests=false`` to skip the test suite.

Everything written along the way stays under the build directory, so
``make clean`` takes all of it with you:

``build/doc/Doxyfile``
    The doxygen configuration, made from ``doc/Doxyfile.in`` with the
    paths filled in.  Edit the ``.in`` file, not this one.

``build/doc/doxygen``
    What doxygen wrote, with the ``xml`` breathe reads next to an
    ``html`` copy you can look at on its own.

``build/doc/html``
    The manual.

Read the Docs has no meson, so ``doc/conf.py`` fills in the same
``Doxyfile.in`` itself and runs doxygen under ``doc/_build`` before
sphinx starts.  That only happens when ``READTHEDOCS`` is set in the
environment.

Testing
=======

Run everything::

    make check

That is ``meson test -C build -v``, which covers the CUnit unit tests
and the Python integration tests.  The integration tests build real
packages with rpmfluff and then run tarpm over them, so rpm-build has
to be installed.

To run one integration test script, name it without the ``test_``
prefix and the ``.py`` suffix::

    make check create

Set ``KEEP=y`` in the environment to hold on to the intermediate files
a test produced::

    make check KEEP=y

Style checks
============

::

    make flake8
    make black

Both of those only look at the Python under ``test/``.

Installing
==========

meson installs the binary and the man page::

    meson install -C build

To build a source package of the latest release instead::

    make srpm
