#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import datetime
import hashlib
import json
import os
import subprocess
import rpmfluff
from baseclass import NAME
from baseclass import REL
from baseclass import VER
from baseclass import TestUnpackRPM
from baseclass import TestUnpackSRPM


def run_tarpm(tarpm, args, env=None):
    """Run tarpm with the given arguments and return (returncode, stdout, stderr)"""
    proc = subprocess.Popen(
        [tarpm] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
    )
    out, err = proc.communicate()

    return (proc.returncode, out.decode(), err.decode())


def query_rpm(pkg, qf):
    """Query an RPM with the given --qf format string and return the output lines"""
    proc = subprocess.Popen(
        ["rpm", "-qp", "--qf", qf, pkg], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    out, err = proc.communicate()

    return out.decode().splitlines()


def file_list(pkg):
    """Return the file list an RPM carries in its header"""
    return query_rpm(pkg, "[%{FILENAMES}\n]")


def digest_list(pkg):
    """Return the (path, digest) pairs an RPM carries in its header"""
    return [line.split() for line in query_rpm(pkg, "[%{FILENAMES} %{FILEDIGESTS}\n]")]


def size_list(pkg):
    """Return the (path, size) pairs an RPM carries in its header"""
    return [
        (path, int(size))
        for (path, size) in [
            line.split() for line in query_rpm(pkg, "[%{FILENAMES} %{FILESIZES}\n]")
        ]
    ]


def mode_list(pkg):
    """Return the (path, mode) pairs an RPM carries in its header"""
    return [
        (path, int(mode, 8))
        for (path, mode) in [
            line.split()
            for line in query_rpm(pkg, "[%{FILENAMES} %{FILEMODES:octal}\n]")
        ]
    ]


def installed_size(pkg):
    """Return the installed size an RPM carries in its header"""
    return int(query_rpm(pkg, "%{LONGSIZE}")[0])


def inode_list(pkg):
    """Return the (path, inode) pairs an RPM carries in its header"""
    return [
        (path, int(inode))
        for (path, inode) in [
            line.split() for line in query_rpm(pkg, "[%{FILENAMES} %{FILEINODES}\n]")
        ]
    ]


def payload_bytes(pkg):
    """Return the uncompressed cpio payload of an RPM"""
    proc = subprocess.Popen(
        ["rpm2cpio", pkg], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    out, err = proc.communicate()

    return out


def read_header(extract_dir):
    """Read the header.json a tarpm extraction wrote"""
    f = open(os.path.join(extract_dir, "header.json"))
    header = json.load(f)
    f.close()

    return header


def write_header(extract_dir, header):
    """Write header.json back to a tarpm extraction"""
    f = open(os.path.join(extract_dir, "header.json"), "w")
    json.dump(header, f, indent=2)
    f.close()


def get_tag(header, name):
    """Return the named tag entry from a header.json structure"""
    for tag in header["tags"]:
        if tag["tag"] == name:
            return tag

    return None


def utc_seconds(year, month, day, hour=12, minute=0, second=0):
    """Return the Unix time of a moment in UTC, noon by default"""
    return int(
        datetime.datetime(
            year, month, day, hour, minute, second, tzinfo=datetime.timezone.utc
        ).timestamp()
    )


class RoundTrip(object):
    """
    Mixin for the round trip tests that runs the extract and create
    halves of tarpm and keeps the paths they work with.  Test classes
    using this are expected to derive from one of the baseclass test
    cases so that self.tarpm and self.output_dir exist.
    """

    def extract(self, pkg, subdir="extracted", env=None):
        """Extract an RPM in to a subdirectory of the test output directory"""
        extract_dir = os.path.join(self.output_dir, subdir)
        os.makedirs(extract_dir)

        rc, out, err = run_tarpm(
            self.tarpm, ["-x", "-f", pkg, "-O", extract_dir], env=env
        )
        self.assertEqual(rc, 0, "Extract failed: %s" % err)

        return extract_dir

    def create_warns(self, extract_dir, name="recreated.rpm", env=None):
        """Create an RPM and return it along with what tarpm said about it"""
        pkg = os.path.join(self.output_dir, name)

        rc, out, err = run_tarpm(self.tarpm, ["-c", "-f", pkg, extract_dir], env=env)
        self.assertEqual(rc, 0, "Create failed: %s" % err)
        self.assertTrue(os.path.isfile(pkg))

        return (pkg, err)

    def create(self, extract_dir, name="recreated.rpm", env=None):
        """Create an RPM from a tarpm extraction directory"""
        pkg, err = self.create_warns(extract_dir, name=name, env=env)

        return pkg

    def create_fails(self, extract_dir, name="recreated.rpm"):
        """Assert tarpm refuses to create an RPM and writes nothing"""
        pkg = os.path.join(self.output_dir, name)

        rc, out, err = run_tarpm(self.tarpm, ["-c", "-f", pkg, extract_dir])
        self.assertNotEqual(rc, 0, "Create unexpectedly succeeded")
        self.assertFalse(os.path.exists(pkg), "A package was written anyway")

        return err

    def payload_path(self, extract_dir, path):
        """Return where a header file list path lands in the payload tree"""
        return os.path.join(extract_dir, "payload", path.lstrip("/"))

    def assertIdentical(self, one, two):
        """Assert two files are byte for byte the same"""
        f = open(one, "rb")
        one_data = f.read()
        f.close()

        f = open(two, "rb")
        two_data = f.read()
        f.close()

        self.assertEqual(
            len(one_data), len(two_data), "%s and %s differ in size" % (one, two)
        )
        self.assertEqual(one_data, two_data, "%s and %s differ" % (one, two))

    def assertVerifies(self, pkg):
        """Assert rpm agrees with the header and payload digests in a package"""
        proc = subprocess.Popen(
            ["rpm", "-Kv", pkg], stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        out, err = proc.communicate()

        self.assertEqual(proc.returncode, 0, "rpm -Kv failed: %s" % err.decode())
        self.assertTrue("Header SHA256 digest: OK" in out.decode())
        self.assertTrue("Payload SHA256 digest: OK" in out.decode())


class TestRoundTripBinaryRPM(RoundTrip, TestUnpackRPM):
    """An unmodified binary RPM comes back byte for byte the same"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/README" % NAME, rpmfluff.SourceFile("README", b"readme\n")
        )
        self.rpm.add_installed_file(
            "/usr/bin/%s" % NAME,
            rpmfluff.SourceFile("%s-bin" % NAME, b"#!/bin/sh\nexit 0\n"),
            mode="755",
        )
        self.rpm.add_installed_symlink("/usr/share/%s/link" % NAME, "README")

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        recreated = self.create(self.extract(original))

        self.assertIdentical(original, recreated)


class TestRoundTripSourceRPM(RoundTrip, TestUnpackSRPM):
    """An unmodified source RPM comes back carrying the same sources"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/README" % NAME, rpmfluff.SourceFile("README", b"readme\n")
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_srpm()

        recreated = self.create(self.extract(original))

        self.assertEqual(file_list(recreated), file_list(original))
        self.assertEqual(digest_list(recreated), digest_list(original))
        self.assertEqual(inode_list(recreated), inode_list(original))
        self.assertEqual(payload_bytes(recreated), payload_bytes(original))
        self.assertVerifies(recreated)


class TestCreateRecomputesFileDigests(RoundTrip, TestUnpackRPM):
    """An edited payload file lands its new digest in the header"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/edited.txt" % NAME,
            rpmfluff.SourceFile("edited.txt", b"before\n"),
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/untouched.txt" % NAME,
            rpmfluff.SourceFile("untouched.txt", b"untouched\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        edited = self.payload_path(extract_dir, "/usr/share/%s/edited.txt" % NAME)

        f = open(edited, "wb")
        f.write(b"after the edit\n")
        f.close()

        recreated = self.create(extract_dir)
        digests = dict(digest_list(recreated))

        self.assertEqual(
            digests["/usr/share/%s/edited.txt" % NAME],
            hashlib.sha256(b"after the edit\n").hexdigest(),
        )
        self.assertEqual(
            digests["/usr/share/%s/untouched.txt" % NAME],
            hashlib.sha256(b"untouched\n").hexdigest(),
        )

        self.assertVerifies(recreated)


class TestCreateRecomputesFileSizes(RoundTrip, TestUnpackRPM):
    """An edited payload file lands its new size in the header"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/grown.txt" % NAME,
            rpmfluff.SourceFile("grown.txt", b"small\n"),
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/shrunk.txt" % NAME,
            rpmfluff.SourceFile("shrunk.txt", b"a much longer line than it keeps\n"),
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/untouched.txt" % NAME,
            rpmfluff.SourceFile("untouched.txt", b"untouched\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        edits = {
            "/usr/share/%s/grown.txt" % NAME: b"a good deal more content than before\n",
            "/usr/share/%s/shrunk.txt" % NAME: b"tiny\n",
        }

        for path, content in edits.items():
            f = open(self.payload_path(extract_dir, path), "wb")
            f.write(content)
            f.close()

        recreated = self.create(extract_dir)
        sizes = dict(size_list(recreated))

        # the header carries what the payload tree actually holds
        for path, content in edits.items():
            self.assertEqual(sizes[path], len(content))

        untouched = "/usr/share/%s/untouched.txt" % NAME
        self.assertEqual(sizes[untouched], len(b"untouched\n"))

        # and the payload carries all of it, not the old number of bytes
        recreated_dir = self.extract(recreated, subdir="recreated_extract")

        for path, content in edits.items():
            f = open(self.payload_path(recreated_dir, path), "rb")
            self.assertEqual(f.read(), content)
            f.close()

        self.assertVerifies(recreated)


class TestCreateKeepsSymlinkSizes(RoundTrip, TestUnpackRPM):
    """A symlink keeps the length of its target as its size"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/README" % NAME, rpmfluff.SourceFile("README", b"readme\n")
        )
        self.rpm.add_installed_symlink("/usr/share/%s/link" % NAME, "README")

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        recreated = self.create(self.extract(original))
        sizes = dict(size_list(recreated))

        self.assertEqual(sizes["/usr/share/%s/link" % NAME], len("README"))
        self.assertEqual(size_list(recreated), size_list(original))
        self.assertVerifies(recreated)


class TestCreateKeepsGhostFileSizes(RoundTrip, TestUnpackRPM):
    """A %ghost file is not in the payload, so it keeps its recorded size"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/real.txt" % NAME, rpmfluff.SourceFile("real.txt", b"real\n")
        )
        self.rpm.add_installed_file(
            "/var/lib/%s/ghost.txt" % NAME,
            rpmfluff.SourceFile("ghost.txt", b"ghostly\n"),
            isGhost=True,
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        ghost = "/var/lib/%s/ghost.txt" % NAME
        self.assertFalse(os.path.exists(self.payload_path(extract_dir, ghost)))

        recreated = self.create(extract_dir)
        sizes = dict(size_list(recreated))

        self.assertEqual(sizes[ghost], dict(size_list(original))[ghost])
        self.assertEqual(sizes["/usr/share/%s/real.txt" % NAME], len(b"real\n"))
        self.assertVerifies(recreated)


class TestCreateHonorsFileDigestAlgo(RoundTrip, TestUnpackRPM):
    """The digest algorithm named in header.json is the one used"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/data.txt" % NAME,
            rpmfluff.SourceFile("data.txt", b"digest me\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        header = read_header(extract_dir)
        tag = get_tag(header, "Filedigestalgo")

        # the algorithm is recorded by name, not by number
        self.assertTrue(tag is not None)
        self.assertEqual(tag["type"], "string")
        self.assertEqual(tag["value"], "sha256")

        tag["value"] = "md5"
        write_header(extract_dir, header)

        recreated = self.create(extract_dir)
        digests = dict(digest_list(recreated))

        # PGPHASHALGO_MD5 is 1
        self.assertEqual(query_rpm(recreated, "%{FILEDIGESTALGO}\n"), ["1"])
        self.assertEqual(
            digests["/usr/share/%s/data.txt" % NAME],
            hashlib.md5(b"digest me\n").hexdigest(),
        )

        self.assertVerifies(recreated)


class TestCreateDropsMissingPayloadFiles(RoundTrip, TestUnpackRPM):
    """A file removed from the payload tree is left out of the header"""

    def setUp(self):
        super().setUp()

        for i in range(4):
            self.rpm.add_installed_file(
                "/usr/share/%s/file%d.txt" % (NAME, i),
                rpmfluff.SourceFile("file%d.txt" % i, b"file %d\n" % i),
            )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        dropped = "/usr/share/%s/file1.txt" % NAME
        os.unlink(self.payload_path(extract_dir, dropped))

        recreated = self.create(extract_dir)
        files = file_list(recreated)

        self.assertTrue(dropped in file_list(original))
        self.assertFalse(dropped in files)
        self.assertEqual(len(files), len(file_list(original)) - 1)

        for i in [0, 2, 3]:
            self.assertTrue("/usr/share/%s/file%d.txt" % (NAME, i) in files)

        # the inodes are the one based positions of what is left
        self.assertEqual(
            [inode for (path, inode) in inode_list(recreated)],
            list(range(1, len(files) + 1)),
        )

        self.assertVerifies(recreated)


class TestCreateKeepsGhostFiles(RoundTrip, TestUnpackRPM):
    """A %ghost file is never in the payload but stays in the header"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/real.txt" % NAME, rpmfluff.SourceFile("real.txt", b"real\n")
        )
        self.rpm.add_installed_file(
            "/var/lib/%s/ghost.txt" % NAME,
            rpmfluff.SourceFile("ghost.txt", b""),
            isGhost=True,
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        ghost = "/var/lib/%s/ghost.txt" % NAME

        # the ghost is in the file list but was never unpacked
        self.assertTrue(ghost in file_list(original))
        self.assertFalse(os.path.exists(self.payload_path(extract_dir, ghost)))

        recreated = self.create(extract_dir)

        self.assertTrue(ghost in file_list(recreated))
        self.assertIdentical(original, recreated)


class TestCreateRenumbersHardLinkInodes(RoundTrip, TestUnpackRPM):
    """Hard links keep sharing an inode number after an entry is dropped"""

    def setUp(self):
        super().setUp()

        for name in ["one.txt", "two.txt", "three.txt"]:
            self.rpm.add_installed_file(
                "/usr/share/%s/%s" % (NAME, name),
                rpmfluff.SourceFile(name, b"the same content\n"),
            )

        self.rpm.add_installed_file(
            "/usr/share/%s/other.txt" % NAME,
            rpmfluff.SourceFile("other.txt", b"something else\n"),
        )

        for name in ["two.txt", "three.txt"]:
            self.rpm.section_install += (
                "ln -f $RPM_BUILD_ROOT/usr/share/%s/one.txt "
                "$RPM_BUILD_ROOT/usr/share/%s/%s\n" % (NAME, NAME, name)
            )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        # rpm gives every hard link of a file the same inode number
        inodes = dict(inode_list(original))
        linked = [
            inodes["/usr/share/%s/%s" % (NAME, name)]
            for name in ["one.txt", "two.txt", "three.txt"]
        ]
        self.assertEqual(len(set(linked)), 1)
        self.assertTrue(inodes["/usr/share/%s/other.txt" % NAME] not in linked)

        extract_dir = self.extract(original)
        os.unlink(self.payload_path(extract_dir, "/usr/share/%s/one.txt" % NAME))

        recreated = self.create(extract_dir)
        files = file_list(recreated)
        inodes = dict(inode_list(recreated))
        other = "/usr/share/%s/other.txt" % NAME

        self.assertFalse("/usr/share/%s/one.txt" % NAME in files)

        # the two remaining links still name one inode, and that is the
        # position the first of them holds in the shortened file list
        links = ["/usr/share/%s/%s" % (NAME, name) for name in ["two.txt", "three.txt"]]
        linked = [inodes[path] for path in links]

        self.assertEqual(len(set(linked)), 1)
        self.assertEqual(linked[0], min(files.index(path) for path in links) + 1)

        # everything else is numbered by its own position
        self.assertEqual(inodes[other], files.index(other) + 1)

        self.assertVerifies(recreated)


class TestRoundTripTagsWrittenByName(RoundTrip, TestUnpackRPM):
    """Tags header.json records by name are read back as themselves"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/data.txt" % NAME,
            rpmfluff.SourceFile("data.txt", b"data\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        header = read_header(extract_dir)

        # the build time is a timestamp and the digest algorithms are names
        buildtime = get_tag(header, "Buildtime")
        self.assertTrue(buildtime is not None)
        self.assertEqual(buildtime["type"], "string")
        self.assertTrue(buildtime["value"].endswith("Z"))

        for name in ["Filedigestalgo", "Payloadsha256algo"]:
            tag = get_tag(header, name)

            if tag is None:
                continue

            self.assertEqual(tag["type"], "string")
            self.assertEqual(tag["value"], "sha256")

        # all of them are an int32 again in the recreated package
        recreated = self.create(extract_dir)

        self.assertEqual(
            query_rpm(recreated, "%{BUILDTIME}\n"),
            query_rpm(original, "%{BUILDTIME}\n"),
        )
        self.assertEqual(query_rpm(recreated, "%{FILEDIGESTALGO}\n"), ["8"])
        self.assertIdentical(original, recreated)


class TagFile(RoundTrip):
    """
    Class for the tests covering the files a file backed tag keeps its
    value in.  Sizing the header and writing it are two passes over
    the same tags and both have to measure it the same way. If the
    sizes do not match, the header allocates more bytes than its index
    entries describe and rpm will not load it.
    """

    def tagfile(self, extract_dir):
        """Return the path to the file holding the Description value"""
        return os.path.join(extract_dir, "description.txt")


class TestCreateEmptyTagFile(TagFile, TestUnpackRPM):
    """An emptied file backed tag becomes an empty value, not a broken header"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/README" % NAME, rpmfluff.SourceFile("README", b"readme\n")
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)

        f = open(self.tagfile(extract_dir), "wb")
        f.close()

        recreated = self.create(extract_dir)

        self.assertEqual(query_rpm(recreated, "<%{DESCRIPTION}>\n"), ["<>"])
        self.assertVerifies(recreated)


class TestCreateRejectsMissingTagFile(TagFile, TestUnpackRPM):
    """A file backed tag naming a file that is gone is an error, not a warning"""

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        os.unlink(self.tagfile(extract_dir))

        self.assertTrue("description.txt" in self.create_fails(extract_dir))


class TestCreateRejectsNulInTagFile(TagFile, TestUnpackRPM):
    """A file backed tag value carrying a NUL is an error, not a broken header"""

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)

        f = open(self.tagfile(extract_dir), "wb")
        f.write(b"ab\0cdefg")
        f.close()

        self.assertTrue("NUL" in self.create_fails(extract_dir))


class Changelog(RoundTrip):
    """
    Class for the tests covering %changelog timestamps.  rpm keeps the
    date of an entry as a count of seconds and tarpm writes it out as
    the date string rpm itself displays, which throws away the time of
    day.  Reading that string back has to land on the same second no
    matter what time zone either half of the round trip runs in.
    """

    # the %changelog the tests build, newest entry first
    entries = [
        ("Tue Mar 15 2022", (2022, 3, 15)),
        ("Wed Jul 29 2020", (2020, 7, 29)),
        ("Mon Jan 04 2016", (2016, 1, 4)),
    ]

    def setUp(self):
        super().setUp()

        # keep rpmbuild from dropping the older entries on age
        self.rpm.header += "%global _changelog_trimtime 0\n"
        self.rpm.header += "%global _changelog_trimage 0\n"

        self.rpm.add_installed_file(
            "/usr/share/%s/README" % NAME, rpmfluff.SourceFile("README", b"readme\n")
        )

        # replace the entry rpmfluff writes so the dates are the ones above
        self.rpm.section_changelog = "".join(
            "* %s Some One <nobody@example.com> - %s-%s\n- entry %d\n\n"
            % (date, VER, REL, i)
            for (i, (date, ymd)) in enumerate(self.entries)
        )

    def build(self):
        """Build the package the changelog tests work with"""
        self.rpm.do_make()

        return self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

    def times(self, pkg):
        """Return the changelog timestamps an RPM carries, newest first"""
        return [int(t) for t in query_rpm(pkg, "[%{CHANGELOGTIME}\n]")]

    def stamps(self, extract_dir):
        """Return the changelog timestamps a tarpm extraction recorded"""
        return [entry["timestamp"] for entry in read_header(extract_dir)["changelog"]]

    def restamp(self, extract_dir, index, timestamp):
        """Replace the timestamp of one changelog entry in header.json"""
        header = read_header(extract_dir)
        header["changelog"][index]["timestamp"] = timestamp
        write_header(extract_dir, header)


class TestRoundTripChangelogTimestamps(Changelog, TestUnpackRPM):
    """Changelog timestamps come back on the same second they went in"""

    def runTest(self):
        original = self.build()

        # rpmbuild dates an entry at noon UTC on the day it names
        self.assertEqual(
            self.times(original), [utc_seconds(*ymd) for (date, ymd) in self.entries]
        )

        extract_dir = self.extract(original)

        # tarpm records the day rpm displays, in the order the header has
        self.assertEqual(
            self.stamps(extract_dir), [date for (date, ymd) in self.entries]
        )

        recreated = self.create(extract_dir)

        self.assertEqual(self.times(recreated), self.times(original))
        self.assertIdentical(original, recreated)


class TestCreateChangelogTimestampIsNoonUTC(Changelog, TestUnpackRPM):
    """An edited changelog date comes back as noon UTC on that day"""

    def runTest(self):
        original = self.build()
        extract_dir = self.extract(original)

        self.restamp(extract_dir, 0, "Thu Jun 01 2023")
        recreated = self.create(extract_dir)

        # only the entry that was edited moves, and it moves to noon
        self.assertEqual(
            self.times(recreated), [utc_seconds(2023, 6, 1)] + self.times(original)[1:]
        )
        self.assertVerifies(recreated)


class TestCreateChangelogTimestampKeepsTimeOfDay(Changelog, TestUnpackRPM):
    """A changelog date written with a time of day keeps that exact second"""

    def runTest(self):
        original = self.build()

        # the same moment named in UTC and in a zone five hours behind it
        for subdir, timestamp, expected in [
            ("utc", "Thu Oct 6 06:48:39 UTC 2016", utc_seconds(2016, 10, 6, 6, 48, 39)),
            (
                "est",
                "Thu Oct 6 06:48:39 EST 2016",
                utc_seconds(2016, 10, 6, 11, 48, 39),
            ),
        ]:
            extract_dir = self.extract(original, subdir="extracted-%s" % subdir)

            # the oldest entry, so the changelog stays in descending order
            self.restamp(extract_dir, 2, timestamp)
            recreated = self.create(extract_dir, name="recreated-%s.rpm" % subdir)

            self.assertEqual(
                self.times(recreated),
                self.times(original)[:2] + [expected],
                "%s did not keep its time of day" % timestamp,
            )
            self.assertVerifies(recreated)


class TestCreateRejectsBadChangelogTimestamp(Changelog, TestUnpackRPM):
    """A changelog date that cannot be read is an error, not 1 Jan 1970"""

    def runTest(self):
        original = self.build()

        # a date rpm cannot be given has to stop the package being written;
        # the alternative is an entry silently dated to the epoch
        for subdir, timestamp, expected in [
            ("garbage", "not a date at all", "not a date at all"),
            ("empty", "", '""'),
            ("truncated", "Tue Mar 15", "Tue Mar 15"),
            ("dayonly", "Tuesday", "Tuesday"),
        ]:
            extract_dir = self.extract(original, subdir="extracted-%s" % subdir)
            self.restamp(extract_dir, 0, timestamp)

            err = self.create_fails(extract_dir, name="recreated-%s.rpm" % subdir)

            self.assertTrue(
                expected in err, "%r was not named in the error: %s" % (timestamp, err)
            )


class TestCreateRejectsMissingChangelogTimestamp(Changelog, TestUnpackRPM):
    """A changelog entry carrying no date at all is an error"""

    def runTest(self):
        original = self.build()
        extract_dir = self.extract(original)

        header = read_header(extract_dir)
        del header["changelog"][1]["timestamp"]
        write_header(extract_dir, header)

        err = self.create_fails(extract_dir)

        self.assertTrue("carries no timestamp" in err, err)


class TestCreateChangelogTimestampHonorsDaylightSaving(Changelog, TestUnpackRPM):
    """A date inside a daylight saving window is read at the right offset"""

    def runTest(self):
        original = self.build()

        # the same wall clock reading in a zone that keeps daylight saving,
        # once inside the window and once outside it
        for subdir, timestamp, expected in [
            (
                "october",
                "Thu Oct 6 06:48:39 America/New_York 2016",
                utc_seconds(2016, 10, 6, 10, 48, 39),
            ),
            (
                "january",
                "Wed Jan 6 06:48:39 America/New_York 2016",
                utc_seconds(2016, 1, 6, 11, 48, 39),
            ),
        ]:
            extract_dir = self.extract(original, subdir="extracted-%s" % subdir)
            self.restamp(extract_dir, 2, timestamp)

            recreated, err = self.create_warns(
                extract_dir, name="recreated-%s.rpm" % subdir
            )

            # a zone the library knows is never warned about
            self.assertFalse("America/New_York" in err, err)
            self.assertEqual(
                self.times(recreated),
                self.times(original)[:2] + [expected],
                "%s was read at the wrong offset" % timestamp,
            )
            self.assertVerifies(recreated)


class TestCreateWarnsOnUnknownChangelogTimeZone(Changelog, TestUnpackRPM):
    """A zone the C library does not know falls back on UTC, and says so"""

    def runTest(self):
        original = self.build()

        # CEST is how a changelog writes central European summer time, but
        # it is not a zone name, so the date can only be read as UTC
        extract_dir = self.extract(original, subdir="extracted-cest")
        self.restamp(extract_dir, 2, "Thu Oct 6 06:48:39 CEST 2016")

        recreated, err = self.create_warns(extract_dir, name="recreated-cest.rpm")

        self.assertTrue("CEST" in err, err)
        self.assertTrue("UTC" in err, err)
        self.assertEqual(
            self.times(recreated),
            self.times(original)[:2] + [utc_seconds(2016, 10, 6, 6, 48, 39)],
        )
        self.assertVerifies(recreated)

        # a zone the library does know is used without any complaint
        extract_dir = self.extract(original, subdir="extracted-known")
        self.restamp(extract_dir, 2, "Thu Oct 6 06:48:39 EST 2016")

        recreated, err = self.create_warns(extract_dir, name="recreated-known.rpm")

        self.assertFalse("EST" in err, err)
        self.assertEqual(
            self.times(recreated),
            self.times(original)[:2] + [utc_seconds(2016, 10, 6, 11, 48, 39)],
        )


class TestChangelogTimestampsIgnoreTimeZone(Changelog, TestUnpackRPM):
    """The time zone tarpm runs in does not move a changelog timestamp"""

    def runTest(self):
        original = self.build()

        for tz in ["UTC", "America/New_York", "Asia/Tokyo", "Pacific/Kiritimati"]:
            env = dict(os.environ, TZ=tz)
            subdir = tz.replace("/", "-")

            extract_dir = self.extract(
                original, subdir="extracted-%s" % subdir, env=env
            )
            self.assertEqual(
                self.stamps(extract_dir),
                [date for (date, ymd) in self.entries],
                "TZ=%s changed what tarpm extracted" % tz,
            )

            recreated = self.create(
                extract_dir, name="recreated-%s.rpm" % subdir, env=env
            )
            self.assertEqual(
                self.times(recreated),
                self.times(original),
                "TZ=%s changed what tarpm created" % tz,
            )
            self.assertIdentical(original, recreated)


class TestCreateAddsNewPayloadFiles(RoundTrip, TestUnpackRPM):
    """Files added to the payload tree land in the new package"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/kept.txt" % NAME, rpmfluff.SourceFile("kept.txt", b"kept\n")
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        base = "/usr/share/%s" % NAME
        added = "%s/added.txt" % base
        newdir = "%s/newdir" % base
        nested = "%s/nested.txt" % newdir
        link = "%s/link" % base

        f = open(self.payload_path(extract_dir, added), "wb")
        f.write(b"added in the payload\n")
        f.close()

        os.mkdir(self.payload_path(extract_dir, newdir))

        f = open(self.payload_path(extract_dir, nested), "wb")
        f.write(b"nested\n")
        f.close()

        os.symlink("kept.txt", self.payload_path(extract_dir, link))

        recreated, err = self.create_warns(extract_dir)
        files = file_list(recreated)
        sizes = dict(size_list(recreated))
        digests = dict([pair for pair in digest_list(recreated) if len(pair) == 2])

        # each new path is named once and only once
        for path in [added, newdir, nested, link]:
            self.assertEqual(err.count("%s is new" % path), 1, err)
            self.assertTrue(path in files, files)

        # the directories the package never owned stay out of the list
        for path in ["/usr", "/usr/share"]:
            self.assertFalse(path in files, files)

        self.assertEqual(sizes[added], len(b"added in the payload\n"))
        self.assertEqual(sizes[nested], len(b"nested\n"))
        self.assertEqual(sizes[link], len("kept.txt"))

        self.assertEqual(
            digests[added], hashlib.sha256(b"added in the payload\n").hexdigest()
        )
        self.assertEqual(digests[nested], hashlib.sha256(b"nested\n").hexdigest())

        # the payload carries them too
        recreated_dir = self.extract(recreated, subdir="recreated_extract")

        self.assertIdentical(
            self.payload_path(extract_dir, added),
            self.payload_path(recreated_dir, added),
        )
        self.assertIdentical(
            self.payload_path(extract_dir, nested),
            self.payload_path(recreated_dir, nested),
        )
        self.assertTrue(os.path.isdir(self.payload_path(recreated_dir, newdir)))
        self.assertEqual(
            os.readlink(self.payload_path(recreated_dir, link)), "kept.txt"
        )

        self.assertVerifies(recreated)


class TestCreateAddsNewSourcePayloadFiles(RoundTrip, TestUnpackSRPM):
    """A file added to a source package payload lands in the new package"""

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_srpm()

        extract_dir = self.extract(original)

        f = open(self.payload_path(extract_dir, "added.patch"), "wb")
        f.write(b"not really a patch\n")
        f.close()

        recreated, err = self.create_warns(extract_dir)
        files = file_list(recreated)
        sizes = dict(size_list(recreated))
        digests = dict([pair for pair in digest_list(recreated) if len(pair) == 2])

        # source package paths carry no leading directory
        self.assertEqual(err.count("added.patch is new"), 1, err)
        self.assertTrue("added.patch" in files, files)
        self.assertFalse("/added.patch" in files, files)

        for path in file_list(original):
            self.assertTrue(path in files, files)

        self.assertEqual(sizes["added.patch"], len(b"not really a patch\n"))
        self.assertEqual(
            digests["added.patch"], hashlib.sha256(b"not really a patch\n").hexdigest()
        )

        self.assertVerifies(recreated)


class TestCreateLeavesUnchangedPayloadAlone(RoundTrip, TestUnpackRPM):
    """Nothing is added when the payload tree is left as it was extracted"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/kept.txt" % NAME, rpmfluff.SourceFile("kept.txt", b"kept\n")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/other.txt" % NAME,
            rpmfluff.SourceFile("other.txt", b"other\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        recreated, err = self.create_warns(self.extract(original))

        self.assertFalse("is new in the payload" in err, err)
        self.assertEqual(file_list(recreated), file_list(original))
        self.assertIdentical(original, recreated)


class TestRoundTripFileTypes(RoundTrip, TestUnpackRPM):
    """The file type of every entry survives a round trip"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/plain.txt" % NAME,
            rpmfluff.SourceFile("plain.txt", b"plain\n"),
        )
        self.rpm.add_installed_directory("/usr/share/%s/emptydir" % NAME)
        self.rpm.add_installed_symlink("usr/share/%s/link" % NAME, "plain.txt")

        # neither of these is ever in the payload
        self.rpm.add_installed_file(
            "/var/lib/%s/ghost.txt" % NAME,
            rpmfluff.SourceFile("ghost.txt", b""),
            isGhost=True,
        )
        self.rpm.add_installed_symlink(
            "var/lib/%s/ghostlink" % NAME, "ghost.txt", isGhost=True
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        types = dict(
            [(f["path"], f["type"]) for f in read_header(extract_dir)["files"]]
        )

        self.assertEqual(types["/usr/share/%s/plain.txt" % NAME], "file")
        self.assertEqual(types["/usr/share/%s/emptydir" % NAME], "dir")
        self.assertEqual(types["/usr/share/%s/link" % NAME], "symlink")
        self.assertEqual(types["/var/lib/%s/ghost.txt" % NAME], "file")
        self.assertEqual(types["/var/lib/%s/ghostlink" % NAME], "symlink")

        recreated = self.create(extract_dir)

        self.assertEqual(mode_list(recreated), mode_list(original))
        self.assertIdentical(original, recreated)


class TestCreateAddedPayloadFileTypes(RoundTrip, TestUnpackRPM):
    """A file added to the payload tree lands with the right type"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/kept.txt" % NAME, rpmfluff.SourceFile("kept.txt", b"kept\n")
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        extract_dir = self.extract(original)
        base = "/usr/share/%s" % NAME

        f = open(self.payload_path(extract_dir, "%s/added.txt" % base), "wb")
        f.write(b"added\n")
        f.close()

        os.mkdir(self.payload_path(extract_dir, "%s/newdir" % base))
        os.symlink("kept.txt", self.payload_path(extract_dir, "%s/link" % base))

        recreated = self.create(extract_dir)
        modes = dict(mode_list(recreated))

        self.assertEqual(modes["%s/added.txt" % base] & 0o170000, 0o100000)
        self.assertEqual(modes["%s/newdir" % base] & 0o170000, 0o040000)
        self.assertEqual(modes["%s/link" % base] & 0o170000, 0o120000)

        # the new entries name their types in the file list as well
        types = dict(
            [
                (f["path"], f["type"])
                for f in read_header(self.extract(recreated, subdir="again"))["files"]
            ]
        )

        self.assertEqual(types["%s/added.txt" % base], "file")
        self.assertEqual(types["%s/newdir" % base], "dir")
        self.assertEqual(types["%s/link" % base], "symlink")

        self.assertVerifies(recreated)


class TestCreateRecomputesInstalledSize(RoundTrip, TestUnpackRPM):
    """The installed size follows what the payload tree holds"""

    def setUp(self):
        super().setUp()

        self.rpm.add_installed_file(
            "/usr/share/%s/kept.txt" % NAME, rpmfluff.SourceFile("kept.txt", b"kept\n")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/edited.txt" % NAME,
            rpmfluff.SourceFile("edited.txt", b"before\n"),
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/gone.txt" % NAME,
            rpmfluff.SourceFile("gone.txt", b"on the way out\n"),
        )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        # rpm adds up the file sizes to get the installed size
        self.assertEqual(
            installed_size(original), sum(size for (path, size) in size_list(original))
        )

        extract_dir = self.extract(original)
        base = "/usr/share/%s" % NAME
        after = b"a good deal longer than before\n"
        added = b"added in the payload\n"

        f = open(self.payload_path(extract_dir, "%s/edited.txt" % base), "wb")
        f.write(after)
        f.close()

        os.unlink(self.payload_path(extract_dir, "%s/gone.txt" % base))

        f = open(self.payload_path(extract_dir, "%s/added.txt" % base), "wb")
        f.write(added)
        f.close()

        recreated = self.create(extract_dir)

        self.assertEqual(
            installed_size(recreated),
            installed_size(original)
            - len(b"before\n")
            - len(b"on the way out\n")
            + len(after)
            + len(added),
        )
        self.assertEqual(
            installed_size(recreated),
            sum(size for (path, size) in size_list(recreated)),
        )
        self.assertVerifies(recreated)


class TestCreateCountsHardLinkSizeOnce(RoundTrip, TestUnpackRPM):
    """Hard links of one another only take up their space once"""

    def setUp(self):
        super().setUp()

        for name in ["one.txt", "two.txt", "three.txt"]:
            self.rpm.add_installed_file(
                "/usr/share/%s/%s" % (NAME, name),
                rpmfluff.SourceFile(name, b"the same content\n"),
            )

        for name in ["two.txt", "three.txt"]:
            self.rpm.section_install += (
                "ln -f $RPM_BUILD_ROOT/usr/share/%s/one.txt "
                "$RPM_BUILD_ROOT/usr/share/%s/%s\n" % (NAME, NAME, name)
            )

    def runTest(self):
        self.rpm.do_make()
        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        # the three links are one file as far as the size goes
        self.assertEqual(installed_size(original), len(b"the same content\n"))

        recreated = self.create(self.extract(original))

        self.assertEqual(installed_size(recreated), installed_size(original))
        self.assertIdentical(original, recreated)
