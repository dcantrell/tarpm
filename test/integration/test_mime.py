#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import os
import subprocess
import unittest
import rpmfluff
from baseclass import NAME
from baseclass import HAVE_RPM_FORMAT_SIX
from baseclass import TestUnpackRPM

try:
    from rpmfluff.utils import get_expected_arch
except ImportError:
    from rpmfluff import get_expected_arch

# Bytes libmagic has nothing to say about, so it calls them
# application/octet-stream.
DATA_BYTES = b"\x00" * 1024


# Base test case for the file MIME types.  A v6 format package carries
# a MIME type for each file where a v4 format one carries a class.
# Each test builds a v6 format package with rpmbuild whatever
# %_rpmformat says, extracts it, changes something in the payload
# tree, and then creates a new v6 format RPM and reads the MIME types
# back out of it.  rpmbuild wrote the types in the original package,
# so those are what tarpm has to match.
@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class TestFileMime(TestUnpackRPM):
    # the format rpmbuild writes and the one we recreate the package as
    build_format = "6"
    create_format = "6"

    def setUp(self):
        super().setUp()
        self.extract_dir = None
        self.recreated_rpm = None
        self.rpm.header += "\n%%global _rpmformat %s\n" % self.build_format

    # Pull the RPM apart in to self.extract_dir
    def extract(self, rpm):
        self.extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(self.extract_dir)
        subprocess.run(
            [self.tarpm, "-x", "-f", rpm, "-O", self.extract_dir], check=True
        )
        return self.extract_dir

    # Put it back together again from self.extract_dir
    def recreate(self):
        self.recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        proc = subprocess.Popen(
            [
                self.tarpm,
                "-c",
                "-F",
                self.create_format,
                "-f",
                self.recreated_rpm,
                self.extract_dir,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")
        return self.recreated_rpm

    # Where a packaged path lives in the payload tree
    def payload_path(self, path):
        return os.path.join(self.extract_dir, "payload", path.lstrip("/"))

    # Read a per file tag of every file in the RPM back through librpm.
    # rpm complains on stderr about a per file tag that does not hold
    # one entry for each file, so that has to stay quiet.
    def query(self, rpm, tag):
        r = {}

        proc = subprocess.Popen(
            [
                "rpm",
                "-qp",
                "--nosignature",
                "--nodigest",
                "--queryformat",
                "[%%{FILENAMES}\t%%{%s}\n]" % tag,
                rpm,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Query failed: {err.decode()}")
        self.assertEqual(err.decode(), "", f"Query complained: {err.decode()}")

        for line in out.decode().splitlines():
            if "\t" not in line:
                continue

            path, name = line.split("\t", 1)

            # rpm prints an empty value as "(none)"
            if name == "(none)":
                name = ""

            r[path] = name

        return r

    # The MIME type of every file in the RPM
    def mimes(self, rpm):
        return self.query(rpm, "FILEMIMES")

    # The MIME type header.json carries for a packaged path, which is
    # no key at all when the file has none
    def json_mime(self, path):
        with open(os.path.join(self.extract_dir, "header.json")) as f:
            data = json.load(f)

        for entry in data["files"]:
            if entry["path"] == path:
                return entry.get("mime", "")

        self.fail("%s is not in the file list" % path)
        return ""


class TestFileMimeUnchanged(TestFileMime):
    """A payload nobody touched keeps the MIME types rpmbuild wrote"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

        empty = rpmfluff.SourceFile("empty.txt", b"")
        self.rpm.add_installed_file("/usr/share/%s/empty.txt" % NAME, empty)

        header = rpmfluff.SourceFile("foo.h", b"#define FOO 1\n")
        self.rpm.add_installed_file("/usr/include/%s/foo.h" % NAME, header)

        script = rpmfluff.SourceFile("run.sh", b"#!/bin/sh\necho hi\n")
        self.rpm.add_installed_file("/usr/bin/%s-run" % NAME, script, mode="755")

    def runTest(self):
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)
        self.recreate()

        before = self.mimes(original_rpm)
        after = self.mimes(self.recreated_rpm)

        self.assertEqual(before, after, "MIME types changed on an untouched payload")

        # and the ones we put there are what rpm works out for itself
        self.assertEqual(before["/usr/share/%s/readme.txt" % NAME], "text/plain")
        self.assertEqual(before["/usr/share/%s/empty.txt" % NAME], "inode/x-empty")
        self.assertEqual(before["/usr/bin/%s-run" % NAME], "text/x-shellscript")

        # rpm answers for a .h without asking libmagic
        self.assertEqual(before["/usr/include/%s/foo.h" % NAME], "text/x-c")


class TestFileMimeChangesWithContent(TestFileMime):
    """Writing new bytes over a payload file works the MIME type out again"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

        other = rpmfluff.SourceFile("other.txt", b"nobody touches this one\n")
        self.rpm.add_installed_file("/usr/share/%s/other.txt" % NAME, other)

    def runTest(self):
        changed = "/usr/share/%s/readme.txt" % NAME
        untouched = "/usr/share/%s/other.txt" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)

        before = self.mimes(original_rpm)
        self.assertEqual(before[changed], "text/plain")

        # plain text becomes a shell script
        with open(self.payload_path(changed), "w") as f:
            f.write("#!/bin/sh\necho hi\n")

        self.recreate()
        after = self.mimes(self.recreated_rpm)

        self.assertEqual(after[changed], "text/x-shellscript")

        # the file nobody touched kept the MIME type it came in with
        self.assertEqual(after[untouched], before[untouched])


class TestFileMimeOnNewPayloadFiles(TestFileMime):
    """A file added to the payload tree picks up a MIME type of its own"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

    def runTest(self):
        docdir = "/usr/share/%s" % NAME
        added = {
            docdir + "/new.txt": b"hello again\n",
            docdir + "/new.html": b"<html><body>hi</body></html>\n",
            docdir + "/new.h": b"#define BAR 2\n",
            docdir + "/new.bin": DATA_BYTES,
        }

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)

        for path, contents in added.items():
            with open(self.payload_path(path), "wb") as f:
                f.write(contents)

        # every file in the list has a MIME type, so query() would
        # catch a list that came out too short
        self.recreate()
        after = self.mimes(self.recreated_rpm)

        for path in added:
            self.assertIn(path, after, "%s never made it in to the file list" % path)

        # what libmagic says about the bytes
        self.assertEqual(after[docdir + "/new.txt"], "text/plain")
        self.assertEqual(after[docdir + "/new.bin"], "application/octet-stream")

        # the path ending is answered without asking libmagic
        self.assertEqual(after[docdir + "/new.html"], "text/html")
        self.assertEqual(after[docdir + "/new.h"], "text/x-c")


class TestFileMimeChangesWithType(TestFileMime):
    """Swapping a file for another kind of thing works the MIME type out again"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

    def runTest(self):
        changed = "/usr/share/%s/readme.txt" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)
        self.assertEqual(self.mimes(original_rpm)[changed], "text/plain")

        # the file becomes a symlink, which libmagic describes as one
        os.unlink(self.payload_path(changed))
        os.symlink("/usr/bin/ls", self.payload_path(changed))

        self.recreate()

        self.assertEqual(self.mimes(self.recreated_rpm)[changed], "inode/symlink")


class TestFileMimeInJSON(TestFileMime):
    """The MIME type in header.json is the one that ends up in the RPM"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

    def runTest(self):
        text = "/usr/share/%s/readme.txt" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)
        self.assertEqual(self.json_mime(text), "text/plain")

        # the MIME type is per file, so it is not one of the tags
        with open(os.path.join(self.extract_dir, "header.json")) as f:
            names = [tag["tag"] for tag in json.load(f)["tags"]]

        self.assertNotIn("Filemimeindex", names)
        self.assertNotIn("Mimedict", names)

        # what we write in header.json is what goes in the package
        with open(os.path.join(self.extract_dir, "header.json")) as f:
            data = json.load(f)

        for entry in data["files"]:
            if entry["path"] == text:
                entry["mime"] = "text/x-made-up"

        with open(os.path.join(self.extract_dir, "header.json"), "w") as f:
            json.dump(data, f, indent=2)

        self.recreate()
        self.assertEqual(self.mimes(self.recreated_rpm)[text], "text/x-made-up")


class TestFileMimeFromFormatFour(TestFileMime):
    """A v4 format package written as v6 format picks up MIME types"""

    build_format = "4"
    create_format = "6"

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

        script = rpmfluff.SourceFile("run.sh", b"#!/bin/sh\necho hi\n")
        self.rpm.add_installed_file("/usr/bin/%s-run" % NAME, script, mode="755")

    def runTest(self):
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)
        self.recreate()

        after = self.mimes(self.recreated_rpm)

        self.assertEqual(after["/usr/share/%s/readme.txt" % NAME], "text/plain")
        self.assertEqual(after["/usr/bin/%s-run" % NAME], "text/x-shellscript")


class TestFileClassFromFormatSix(TestFileMime):
    """A v6 format package written as v4 format picks up classes"""

    build_format = "6"
    create_format = "4"

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

        script = rpmfluff.SourceFile("run.sh", b"#!/bin/sh\necho hi\n")
        self.rpm.add_installed_file("/usr/bin/%s-run" % NAME, script, mode="755")

    def runTest(self):
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        self.extract(original_rpm)
        self.recreate()

        after = self.query(self.recreated_rpm, "FILECLASS")

        self.assertEqual(after["/usr/share/%s/readme.txt" % NAME], "ASCII text")
        self.assertIn("script", after["/usr/bin/%s-run" % NAME])
