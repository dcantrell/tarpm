#!/bin/sh
#
# Quick n' Dirty script to kick out a variety of mock builds on the
# host architecture.
#

PATH=/usr/bin
CWD="$(pwd)"
ARCH="$(uname -m)"

if [ ! -d /etc/mock ]; then
    echo "*** You need mock(1) installed for these builds." >&2
    exit 1
fi

# clean and build archive and SRPM
make clean
rm -f tarpm-*.tar.gz tarpm-*.src.rpm
make copr-srpm

# collect the archive names
SRC="$(/bin/ls -1 tarpm-*.tar.gz)"
SRPM="$(/bin/ls -1 tarpm-*.src.rpm)"

# build for some mock targets
cd /etc/mock || exit 1
for target in fedora*${ARCH}.cfg centos-stream+epel*${ARCH}.cfg ; do
    [ -L "${target}" ] && continue
    rname="$(basename "${target}")"
    mock -r "${rname}" --rebuild "${CWD}"/"${SRPM}"
done
