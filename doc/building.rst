========
Building
========

Requirements
============

tarpm is written in C and builds with meson and ninja.  The libraries
it needs are:

* librpm, for the tag tables and a few helpers
* json-c, for reading and writing the JSON metadata
* libarchive 3.3.3 or later, for the cpio payload
* libmagic, for determining file classes and MIME types
* libzstd, for zstd payload compression
* liblzma, for xz payload compression
* OpenSSL, for the digests

The test suite also wants CUnit, python3, python3-rpm, rpm-build, and
the rpmfluff Python module.  It checks the man page with groff and
mandoc as well, and skips that check when either one is missing.
The HTML manual wants doxygen, sphinx, breathe, and the Read the Docs
sphinx theme.

On Fedora the packages are listed in ``osdeps/fedora/reqs.txt`` and
the Python modules in ``osdeps/fedora/pip.txt``.  This installs both
of them::

    make instreqs

Building
========

The tree uses meson, but there is a GNUmakefile wrapping the common
steps so you do not have to remember them::

    make

That runs ``meson setup build`` and then ``ninja -C build``.  The
binary lands at ``build/src/tarpm`` and runs out of the build
directory without installing anything.

For a debug build with warnings as errors::

    make debug

To start over::

    make clean

Run ``make help`` for the rest of the targets.

Documentation
=============

These documents and the man page build along with everything else, so
a plain ``make`` writes the HTML manual to ``build/doc/html``.  To
build only that::

    make docs

doxygen turns the Doxygen comments in ``include/`` and ``src/`` in to
XML and sphinx reads that through breathe alongside the rst files in
``doc/``.  The API lives in ``include/``, so that is where the
function, type and constant documentation sits.  Each file in
``src/`` carries a ``@file`` block saying what that source file does.
The static helpers inside ``src/`` are left alone on purpose, and
``include/compat/queue.h`` is skipped because it is an imported copy
of the BSD ``sys/queue.h``.

A missing piece skips the manual with a warning rather than failing
the build of the program, so you can always build tarpm without any
of it.  To turn it off outright::

    meson setup build -Ddocs=false

The other meson options are ``-Dnls=false`` to skip translations and
``-Dtests=false`` to skip the test suite.

Everything written along the way stays under the build directory, so
``make clean`` takes all of it with you:

``build/doc/Doxyfile``
    The doxygen configuration, made from ``doc/Doxyfile.in`` with the
    paths filled in.  Edit the ``.in`` file rather than this one.

``build/doc/doxygen``
    What doxygen wrote, with the ``xml`` breathe reads sitting next to
    an ``html`` copy you can look at on its own.

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
packages with rpmfluff and then run tarpm over them, so you need
rpm-build installed.

To run a single integration test script, name it without the
``test_`` prefix and the ``.py`` suffix::

    make check create

Set ``KEEP=y`` in the environment to hold on to the intermediate files
a test produced::

    make check KEEP=y

Style checks
============

::

    make flake8
    make black
    make shellcheck

The first two only look at the Python under ``test/``.
``make shellcheck`` looks at the shell scripts under ``utils/`` and
``osdeps/``.

Installing
==========

meson installs the binary and the man page::

    meson install -C build

To build a source package of the latest release instead::

    make srpm

This archives the ``vX.Y`` tag matching the version in ``meson.build``.
It stops with an error when that tag does not exist, so a version bump
nobody tagged yet cannot ship old source under the new version.  Tag
the release with ``make release`` first.

For a source package of the working tree instead::

    make copr-srpm
