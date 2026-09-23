#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import os
import subprocess
import rpmfluff
from baseclass import NAME
from baseclass import TestUnpackRPM

# Bytes libmagic has nothing to say about, so it calls them "data",
# which is not a class rpm keeps.
DATA_BYTES = b"\x00" * 1024


# Base test case for the file class strings.  Each test builds a
# package with rpmbuild, extracts it, changes something in the payload
# tree, and then creates a new RPM and reads the file classes back out
# of it.  rpmbuild wrote the classes in the original package, so the
# classes in the original are what tarpm has to match.
class TestFileClass(TestUnpackRPM):
    def setUp(self):
        super().setUp()
        self.extract_dir = None
        self.recreated_rpm = None

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
            [self.tarpm, "-c", "-f", self.recreated_rpm, self.extract_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")
        return self.recreated_rpm

    # Where a packaged path lives in the payload tree
    def payload_path(self, path):
        return os.path.join(self.extract_dir, "payload", path.lstrip("/"))

    # The class of every file in the RPM, read back through librpm so
    # that RPMTAG_FILECLASS and RPMTAG_CLASSDICT both have to be right.
    #
    # Note that %{FILECLASS} is worked out rather than read straight
    # out of the header.  When the stored class is empty, rpm makes one
    # up from the file mode, and for a symlink it makes one up from the
    # link target.  See makeFClass() in lib/tagexts.cc in the rpm
    # source.  Use stored_classes() when that gets in the way.
    def classes(self, rpm):
        r = {}

        proc = subprocess.Popen(
            [
                "rpm",
                "-qp",
                "--nosignature",
                "--nodigest",
                "--queryformat",
                "[%{FILENAMES}\t%{FILECLASS}\n]",
                rpm,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Query failed: {err.decode()}")

        for line in out.decode().splitlines():
            if "\t" not in line:
                continue

            path, name = line.split("\t", 1)

            # rpm prints an empty class as "(none)"
            if name == "(none)":
                name = ""

            r[path] = name

        return r

    # The class header.json carries for a packaged path, which is no
    # key at all when the file has no class
    def json_class(self, path, directory=None):
        if directory is None:
            directory = self.extract_dir

        with open(os.path.join(directory, "header.json")) as f:
            data = json.load(f)

        for entry in data["files"]:
            if entry["path"] == path:
                return entry.get("class", "")

        self.fail("%s is not in the file list" % path)
        return ""

    # The class an RPM really carries, rather than the one rpm makes up
    # at query time.  Pull the RPM apart again and read header.json.
    def stored_classes(self, rpm):
        r = {}
        directory = os.path.join(self.output_dir, "stored")

        os.makedirs(directory)
        subprocess.run([self.tarpm, "-x", "-f", rpm, "-O", directory], check=True)

        with open(os.path.join(directory, "header.json")) as f:
            data = json.load(f)

        for entry in data["files"]:
            r[entry["path"]] = entry.get("class", "")

        return r


class TestFileClassUnchanged(TestFileClass):
    """A payload nobody touched keeps the classes rpmbuild wrote"""

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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)
        self.recreate()

        before = self.classes(original_rpm)
        after = self.classes(self.recreated_rpm)

        self.assertEqual(before, after, "classes changed on an untouched payload")

        # and the ones we put there are what rpm works out for itself
        self.assertEqual(before["/usr/share/%s/readme.txt" % NAME], "ASCII text")
        self.assertEqual(before["/usr/share/%s/empty.txt" % NAME], "empty")
        self.assertIn("script", before["/usr/bin/%s-run" % NAME])

        # "C Header" holds no token rpm keeps, so the file has no class
        self.assertEqual(before["/usr/include/%s/foo.h" % NAME], "")


class TestFileClassChangesWithContent(TestFileClass):
    """Writing new bytes over a payload file works the class out again"""

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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)

        before = self.classes(original_rpm)
        self.assertEqual(before[changed], "ASCII text")

        # plain text becomes a shell script
        with open(self.payload_path(changed), "w") as f:
            f.write("#!/bin/sh\necho hi\n")

        self.recreate()
        after = self.classes(self.recreated_rpm)

        self.assertIn("script", after[changed])
        self.assertNotEqual(after[changed], before[changed])

        # the file nobody touched kept the class it came in with
        self.assertEqual(after[untouched], before[untouched])


class TestFileClassGoesAwayWithContent(TestFileClass):
    """A file whose new bytes have no class rpm keeps loses its class"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

    def runTest(self):
        changed = "/usr/share/%s/readme.txt" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)
        self.assertEqual(self.classes(original_rpm)[changed], "ASCII text")

        # bytes libmagic calls data, which rpm does not keep
        with open(self.payload_path(changed), "wb") as f:
            f.write(DATA_BYTES)

        self.recreate()

        self.assertEqual(self.classes(self.recreated_rpm)[changed], "")


class TestFileClassOnNewPayloadFiles(TestFileClass):
    """A file added to the payload tree picks up a class of its own"""

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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)

        for path, contents in added.items():
            with open(self.payload_path(path), "wb") as f:
                f.write(contents)

        self.recreate()
        after = self.classes(self.recreated_rpm)

        for path in added:
            self.assertIn(path, after, "%s never made it in to the file list" % path)

        # what libmagic says about the bytes
        self.assertEqual(after[docdir + "/new.txt"], "ASCII text")

        # the path ending is answered without asking libmagic
        self.assertEqual(after[docdir + "/new.html"], "HTML document")

        # "C Header" holds no token rpm keeps
        self.assertEqual(after[docdir + "/new.h"], "")

        # neither does "data"
        self.assertEqual(after[docdir + "/new.bin"], "")


class TestFileClassChangesWithType(TestFileClass):
    """Swapping a file for another kind of thing works the class out again"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

    def runTest(self):
        changed = "/usr/share/%s/readme.txt" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)
        self.assertEqual(self.classes(original_rpm)[changed], "ASCII text")

        # the file becomes a symlink, which rpm records no class for
        os.unlink(self.payload_path(changed))
        os.symlink("/usr/bin/ls", self.payload_path(changed))

        self.recreate()

        self.assertEqual(self.stored_classes(self.recreated_rpm)[changed], "")

        # rpm makes a class up from the link target on the way out
        self.assertEqual(
            self.classes(self.recreated_rpm)[changed],
            "symbolic link to `/usr/bin/ls'",
        )


class TestFileClassInJSON(TestFileClass):
    """The class in header.json is the one that ends up in the RPM"""

    def setUp(self):
        super().setUp()

        text = rpmfluff.SourceFile("readme.txt", b"hello world\n")
        self.rpm.add_installed_file("/usr/share/%s/readme.txt" % NAME, text)

        header = rpmfluff.SourceFile("foo.h", b"#define FOO 1\n")
        self.rpm.add_installed_file("/usr/include/%s/foo.h" % NAME, header)

    def runTest(self):
        text = "/usr/share/%s/readme.txt" % NAME
        header = "/usr/include/%s/foo.h" % NAME

        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

        self.extract(original_rpm)

        # a file with no class has no class key at all
        self.assertEqual(self.json_class(text), "ASCII text")
        self.assertEqual(self.json_class(header), "")

        self.recreate()
        after = self.classes(self.recreated_rpm)

        self.assertEqual(after[text], self.json_class(text))
        self.assertEqual(after[header], self.json_class(header))
