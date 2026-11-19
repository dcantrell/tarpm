========================
Listing package contents
========================

``-t`` lists the payload and writes nothing to disk::

    $ tarpm -tvf bash.rpm
    -rw-r--r-- root/root           18 2026-01-16 00:00 ./etc/skel/.bash_logout
    -rw-r--r-- root/root          144 2026-01-16 00:00 ./etc/skel/.bash_profile
    -rw-r--r-- root/root          522 2026-01-16 00:00 ./etc/skel/.bashrc
    -rwxr-xr-x root/root           33 2026-01-16 00:00 ./usr/bin/alias
    -rwxr-xr-x root/root      1551104 2026-01-16 00:00 ./usr/bin/bash
    lrwxrwxrwx root/root           10 2026-01-16 00:00 ./usr/bin/bashbug

The columns are the file type and permissions, the owner and group,
the size in bytes, the modification time, and the path.  The ownership
and size columns grow as wide as the widest entry in the package needs
them to be.  Paths carry a leading ``./`` because that is how they are
represented in the cpio archive.

A control character in a path is printed as a ``\ooo`` octal escape
and a backslash is printed as ``\\``, which is what ``ls -b`` does.
This keeps a crafted file name from sending escape sequences to your
terminal.  The paths ``-xv`` prints and the paths in warnings are
escaped the same way.

tarpm always prints the long form of the listing, so ``-tf`` and
``-tvf`` give you the same output.  ``-v`` is accepted here for the
sake of muscle memory.

The first character of the mode is the file type.  ``-`` is a regular
file, ``d`` is a directory, ``l`` is a symlink, ``c`` and ``b`` are
character and block device nodes, ``p`` is a pipe, and ``s`` is a
socket.

A few things are worth knowing about this listing:

* The owner and group are names rather than numbers.  RPM records
  names in the header so a package works on systems that number their
  users differently.
* The size of a symlink is the length of its target.
* This walks the payload archive and not the header file list.  A
  ``%ghost`` file has no bytes in the payload, so you will see it in
  ``rpm -qlp`` output but not in this one.  Extract the package if you
  want to see the ghosts.

List mode shows you none of the header metadata.  For that you need to
extract the package.  See :doc:`extracting`.

A file that is not there, or that is not an RPM, makes tarpm print an
error naming it and exit non-zero.  Nothing gets listed.
