tarpm

A command line tool to create and extract binary RPM files.  The RPM
header data is written to a set of JSON files.  You can edit the
metadata before repacking the tree in to a binary RPM.  Things like
checksums can be recomputed as necessary so you can make changes to
the RPM payload.

The intent of this project is not to replace tools like rpmbuild, but
rather to add an additional tool for specific RPM modification as
well as an alternative way to construct binary RPMs.


EXAMPLES
========


NAME
====

The tar(1) command is generally understood for creating archive files
of directory trees.  Tools like jar(1) are sort of a play on tar(1)
but for Java jar files.  Initially I thought of calling this program
rar(1) for "RPM archiver", but rar seems to be used elsewhere...for
something familiar.  I landed on "tarpm" (pronounced as TAR PEE EMM)
because it describes the program, provides a short command line tool
name, and is sort of amusing.  Naming software projects is the hardest
part of any project.  Aside from testing and documentation.


LICENSE
=======

The GNU General Public License 3.0 (or, at your option, any later
version) and the GNU Lesser General Public License 3.0 (or, at your
option, any later version) cover this project.  Some code is licensed
under the GPL-3.0-or-later and some code is under the
LGPL-3.0-or-later.  I was able to copy in code from my other projects,
such as rpminspect, when it fit the need in tarpm.

See the COPYING and COPYING.LIB files for more information.
