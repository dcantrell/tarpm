#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import re
import subprocess
import rpmfluff
from baseclass import NAME
from baseclass import RequiresTarpm
from baseclass import TestUnpackSRPM, TestUnpackRPM


def list_entries(out):
    """Turn the output of tarpm -t in to a dict of payload path to fields"""
    entries = {}

    for line in out.decode("utf-8").splitlines():
        fields = line.split(None, 5)

        if len(fields) != 6:
            continue

        entries[fields[5]] = {
            "mode": fields[0],
            "owner": fields[1],
            "size": int(fields[2]),
            "date": fields[3],
            "time": fields[4],
        }

    return entries


class ListPayload(object):
    """
    Mixin for the list tests that checks the shape of what tarpm -t
    reports.  Test classes using this are expected to derive from one
    of the baseclass test cases.
    """

    def assertEntry(self, entries, path, mode=None, size=None):
        """Assert the payload path was listed and check what was said about it"""
        self.assertTrue(path in entries, "%s was not listed" % path)

        entry = entries[path]

        if mode is not None:
            self.assertEqual(
                entry["mode"], mode, "%s has mode %s" % (path, entry["mode"])
            )

        if size is not None:
            self.assertEqual(
                entry["size"], size, "%s is %d bytes" % (path, entry["size"])
            )

        # every entry carries the ownership and the modification time
        self.assertEqual(entry["owner"], "root/root")
        self.assertTrue(re.fullmatch(r"\d{4}-\d{2}-\d{2}", entry["date"]))
        self.assertTrue(re.fullmatch(r"\d{2}:\d{2}", entry["time"]))

        return


class VerifyListSimpleRPM(ListPayload, TestUnpackRPM):
    """Test listing a simple RPM with basic files"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        readme = rpmfluff.SourceFile("README", b"x" * 1024)
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

        binary = rpmfluff.SourceFile("%s-bin" % NAME, b"y" * 2048)
        self.rpm.add_installed_file("/usr/bin/%s" % NAME, binary, mode="755")

    def runTest(self):
        super().runTest()

        # listing says nothing on stderr and writes nothing to disk
        self.assertEqual(self.err.decode("utf-8"), "")
        self.assertFalse(os.path.exists(os.path.join(self.output_dir, "payload")))

        entries = list_entries(self.out)

        # every line of the listing describes one payload entry
        self.assertEqual(len(entries), len(self.out.decode("utf-8").splitlines()))

        # the payload paths are reported the way tar(1) reports them
        self.assertEntry(
            entries,
            "./usr/share/doc/%s/README" % NAME,
            mode="-rw-r--r--",
            size=1024,
        )
        self.assertEntry(entries, "./usr/bin/%s" % NAME, mode="-rwxr-xr-x", size=2048)


class VerifyListFilePermissions(ListPayload, TestUnpackRPM):
    """Test listing reports the mode of each payload entry"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        self.modes = ["644", "755", "600", "444"]

        for mode in self.modes:
            f = rpmfluff.SourceFile("file%s.txt" % mode, b"content\n")
            self.rpm.add_installed_file(
                "/usr/share/%s/file%s.txt" % (NAME, mode), f, mode=mode
            )

    def runTest(self):
        super().runTest()

        entries = list_entries(self.out)
        expected = {
            "644": "-rw-r--r--",
            "755": "-rwxr-xr-x",
            "600": "-rw-------",
            "444": "-r--r--r--",
        }

        for mode in self.modes:
            self.assertEntry(
                entries,
                "./usr/share/%s/file%s.txt" % (NAME, mode),
                mode=expected[mode],
                size=8,
            )


class VerifyListDirectory(ListPayload, TestUnpackRPM):
    """Test listing an RPM that owns a directory"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        self.rpm.add_installed_directory("/usr/share/%s/dir" % NAME)

    def runTest(self):
        super().runTest()

        entries = list_entries(self.out)

        # a directory is listed with no size of its own
        self.assertEntry(
            entries, "./usr/share/%s/dir" % NAME, mode="drwxr-xr-x", size=0
        )


class VerifyListSymlink(ListPayload, TestUnpackRPM):
    """Test listing an RPM holding a symbolic link"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        original = rpmfluff.SourceFile("original.txt", b"original\n")
        self.rpm.add_installed_file("/usr/share/%s/original.txt" % NAME, original)
        self.rpm.add_installed_symlink("/usr/share/%s/link.txt" % NAME, "original.txt")

    def runTest(self):
        super().runTest()

        entries = list_entries(self.out)

        self.assertEntry(
            entries, "./usr/share/%s/original.txt" % NAME, mode="-rw-r--r--", size=9
        )

        # a symbolic link is as big as the target it names
        self.assertEntry(
            entries,
            "./usr/share/%s/link.txt" % NAME,
            mode="lrwxrwxrwx",
            size=len("original.txt"),
        )


class VerifyListEmptyFile(ListPayload, TestUnpackRPM):
    """Test listing an RPM holding an empty file"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        empty = rpmfluff.SourceFile("empty.txt", b"")
        self.rpm.add_installed_file("/usr/share/%s/empty.txt" % NAME, empty)

    def runTest(self):
        super().runTest()

        entries = list_entries(self.out)
        self.assertEntry(entries, "./usr/share/%s/empty.txt" % NAME, size=0)


class VerifyListSRPM(ListPayload, TestUnpackSRPM):
    """Test listing the payload of a source RPM"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        source = rpmfluff.SourceFile("source.tar.gz", b"fake tarball content")
        self.rpm.add_source(source)

    def runTest(self):
        super().runTest()

        entries = list_entries(self.out)

        # an SRPM payload is flat, so the spec file and the sources sit at the top
        self.assertEntry(entries, "./%s.spec" % NAME)
        self.assertEntry(entries, "./source.tar.gz", size=len(b"fake tarball content"))


class VerifyListShortSyntax(TestUnpackRPM):
    """Test the tar(1) style short syntax for listing"""

    def setUp(self):
        super().setUp()
        self.mode = "-t"

        readme = rpmfluff.SourceFile("README", b"readme\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # "tvf FILE" lists the same payload "-t -f FILE" does
        proc = subprocess.Popen(
            [
                self.tarpm,
                "tvf",
                self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch()),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()

        self.assertEqual(proc.returncode, 0, "Listing failed: %s" % err.decode("utf-8"))
        self.assertEqual(list_entries(out), list_entries(self.out))


class VerifyListNotAnRPM(RequiresTarpm):
    """Test listing something that is not an RPM"""

    def runTest(self):
        proc = subprocess.Popen(
            [self.tarpm, "-t", "-f", self.tarpm],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()

        # nothing is listed and tarpm says why
        self.assertEqual(out.decode("utf-8"), "")
        self.assertNotEqual(err.decode("utf-8").find("rpmReadPackageFile"), -1)
