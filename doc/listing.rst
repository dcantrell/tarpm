========================
Listing package contents
========================

``-t`` lists the payload without writing anything to disk::

    $ tarpm -tvf bash.rpm
    -rw-r--r-- root/root           18 2026-01-16 00:00 ./etc/skel/.bash_logout
    -rw-r--r-- root/root          144 2026-01-16 00:00 ./etc/skel/.bash_profile
    -rw-r--r-- root/root          522 2026-01-16 00:00 ./etc/skel/.bashrc
    -rwxr-xr-x root/root           33 2026-01-16 00:00 ./usr/bin/alias
    -rwxr-xr-x root/root      1551104 2026-01-16 00:00 ./usr/bin/bash
    lrwxrwxrwx root/root           10 2026-01-16 00:00 ./usr/bin/bashbug

The columns are the file type and permissions, the owner and group, the
size in bytes, the modification time, and the path.  The ownership and
size columns are as wide as the widest entry in the package needs them
to be.  Paths carry a leading ``./`` because that is how they sit in the
cpio archive.

tarpm always prints the long form, so ``-tf`` and ``-tvf`` give you the
same thing.  ``-v`` is accepted for the sake of muscle memory.

The first character of the mode is the file type.  ``-`` is a regular
file, ``d`` a directory, ``l`` a symlink, ``c`` and ``b`` are character
and block device nodes, ``p`` a pipe, and ``s`` a socket.

A few things worth knowing about this listing:

* The owner and group are names, not numbers.  RPM records names in the
  header so a package works on systems that number their users
  differently.
* The size of a symlink is the length of its target.
* This walks the payload archive, not the header file list.  A
  ``%ghost`` file has no bytes in the payload, so it is in
  ``rpm -qlp`` output and not in this one.  Extract the package if you
  want to see the ghosts.

List mode shows none of the header metadata.  For that, extract the
package.  See :doc:`extracting`.
