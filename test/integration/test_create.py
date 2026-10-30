#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import os
import platform
import shutil
import subprocess
import unittest
import rpmfluff
from baseclass import NAME
from baseclass import HAVE_RPM_FORMAT_SIX
from baseclass import RequiresTarpm
from baseclass import TestUnpackRPM

try:
    from rpmfluff.utils import get_expected_arch
except ImportError:
    from rpmfluff import get_expected_arch


def make_random_content(size):
    """Generate random printable ASCII content of given size"""
    import random
    import string

    return "".join(random.choices(string.ascii_letters + string.digits + " \n", k=size))


class TestCreateRPMRoundTrip(TestUnpackRPM):
    """
    Test creating an RPM by:
    1. Building an RPM with rpmbuild (via rpmfluff)
    2. Extracting it with tarpm
    3. Creating a new RPM with tarpm from the extracted directory
    4. Comparing both RPMs to ensure they contain the same payload
    """

    def setUp(self):
        super().setUp()

        # Add some files to make a realistic package
        readme_content = make_random_content(1024)
        readme = rpmfluff.SourceFile("README", readme_content.encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

        binary_content = make_random_content(2048)
        binary = rpmfluff.SourceFile("%s-bin" % NAME, binary_content.encode("utf-8"))
        self.rpm.add_installed_file("/usr/bin/%s" % NAME, binary, mode="755")

        config_content = make_random_content(512)
        config = rpmfluff.SourceFile("config.conf", config_content.encode("utf-8"))
        self.rpm.add_installed_file("/etc/%s/config.conf" % NAME, config)

    def runTest(self):
        # Step 1: Build RPM with rpmbuild
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        # Step 2: Extract the original RPM with tarpm
        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)

        args = [
            self.tarpm,
            "-x",
            "-f",
            original_rpm,
            "-O",
            extract_dir,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Extract failed: {err.decode()}")

        # Verify extraction created expected structure
        self.assertTrue(os.path.isdir(os.path.join(extract_dir, "payload")))
        self.assertTrue(os.path.isfile(os.path.join(extract_dir, "header.json")))
        self.assertTrue(os.path.isfile(os.path.join(extract_dir, "signature.json")))
        self.assertTrue(os.path.isfile(os.path.join(extract_dir, "lead.json")))

        # Step 3: Create a new RPM with tarpm from the extracted directory
        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")

        args = [
            self.tarpm,
            "-c",
            "-f",
            recreated_rpm,
            extract_dir,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        # Verify the new RPM was created
        self.assertTrue(os.path.isfile(recreated_rpm))
        self.assertGreater(os.path.getsize(recreated_rpm), 0)

        # Step 4: Extract both RPMs and compare their payloads
        original_extract = os.path.join(self.output_dir, "original_payload")
        os.makedirs(original_extract)

        args = [
            self.tarpm,
            "-x",
            "-f",
            original_rpm,
            "-O",
            original_extract,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        proc.communicate()
        self.assertEqual(proc.returncode, 0)

        recreated_extract = os.path.join(self.output_dir, "recreated_payload")
        os.makedirs(recreated_extract)

        args = [
            self.tarpm,
            "-x",
            "-f",
            recreated_rpm,
            "-O",
            recreated_extract,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        proc.communicate()
        self.assertEqual(proc.returncode, 0)

        # Compare payload directories
        self._compare_directories(
            os.path.join(original_extract, "payload"),
            os.path.join(recreated_extract, "payload"),
        )

    def _compare_directories(self, dir1, dir2):
        """Recursively compare two directory trees"""
        # Get all files in both directories
        files1 = set()
        for root, dirs, files in os.walk(dir1):
            for f in files:
                rel_path = os.path.relpath(os.path.join(root, f), dir1)
                files1.add(rel_path)

        files2 = set()
        for root, dirs, files in os.walk(dir2):
            for f in files:
                rel_path = os.path.relpath(os.path.join(root, f), dir2)
                files2.add(rel_path)

        # Verify same set of files
        self.assertEqual(files1, files2, "File lists differ between payloads")

        # Compare each file
        for rel_path in files1:
            file1 = os.path.join(dir1, rel_path)
            file2 = os.path.join(dir2, rel_path)

            # Compare file sizes
            size1 = os.path.getsize(file1)
            size2 = os.path.getsize(file2)
            self.assertEqual(
                size1, size2, f"File size mismatch for {rel_path}: {size1} vs {size2}"
            )

            # Compare file contents
            with open(file1, "rb") as f1, open(file2, "rb") as f2:
                content1 = f1.read()
                content2 = f2.read()
                self.assertEqual(
                    content1, content2, f"File content mismatch for {rel_path}"
                )

            # Compare file permissions
            mode1 = os.stat(file1).st_mode & 0o777
            mode2 = os.stat(file2).st_mode & 0o777
            self.assertEqual(
                mode1,
                mode2,
                f"File permission mismatch for {rel_path}: {oct(mode1)} vs {oct(mode2)}",
            )


class TestCreateRPMWithSymlinks(TestUnpackRPM):
    """Test creating an RPM with symbolic links"""

    def setUp(self):
        super().setUp()

        # Add a regular file and a symlink
        original = rpmfluff.SourceFile(
            "original.txt", make_random_content(1024).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/%s/original.txt" % NAME, original)
        self.rpm.add_installed_symlink("/usr/share/%s/link.txt" % NAME, "original.txt")

    def runTest(self):
        # Build original RPM
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        # Extract with tarpm
        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)

        args = [self.tarpm, "-x", "-f", original_rpm, "-O", extract_dir]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        proc.communicate()
        self.assertEqual(proc.returncode, 0)

        # Create new RPM with tarpm
        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        args = [self.tarpm, "-c", "-f", recreated_rpm, extract_dir]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        # Extract the recreated RPM
        recreated_extract = os.path.join(self.output_dir, "recreated_extract")
        os.makedirs(recreated_extract)

        args = [self.tarpm, "-x", "-f", recreated_rpm, "-O", recreated_extract]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        proc.communicate()
        self.assertEqual(proc.returncode, 0)

        # Verify symlink exists and is correct
        payload_dir = os.path.join(recreated_extract, "payload")
        link_path = os.path.join(payload_dir, "usr/share", NAME, "link.txt")
        original_path = os.path.join(payload_dir, "usr/share", NAME, "original.txt")

        self.assertTrue(os.path.islink(link_path))
        self.assertEqual(os.readlink(link_path), "original.txt")
        self.assertTrue(os.path.isfile(original_path))


class TestCreateRPMWithDirectories(TestUnpackRPM):
    """Test creating an RPM with nested directory structures"""

    def setUp(self):
        super().setUp()

        # Add files in nested directories
        for i in range(3):
            content = rpmfluff.SourceFile(
                f"file{i}.txt", make_random_content(100 * (i + 1)).encode("utf-8")
            )
            self.rpm.add_installed_file(
                f"/var/lib/{NAME}/subdir{i}/file{i}.txt", content
            )

    def runTest(self):
        # Build, extract, recreate, and re-extract
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)
        subprocess.run(
            [self.tarpm, "-x", "-f", original_rpm, "-O", extract_dir], check=True
        )

        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", recreated_rpm, extract_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        recreated_extract = os.path.join(self.output_dir, "recreated_extract")
        os.makedirs(recreated_extract)
        subprocess.run(
            [self.tarpm, "-x", "-f", recreated_rpm, "-O", recreated_extract], check=True
        )

        # Verify all directories were created
        payload_dir = os.path.join(recreated_extract, "payload")
        for i in range(3):
            subdir = os.path.join(payload_dir, "var/lib", NAME, f"subdir{i}")
            self.assertTrue(os.path.isdir(subdir))

            file_path = os.path.join(subdir, f"file{i}.txt")
            self.assertTrue(os.path.isfile(file_path))
            self.assertEqual(os.path.getsize(file_path), 100 * (i + 1))


class TestCreateRPMPreservesPermissions(TestUnpackRPM):
    """Test that creating an RPM preserves file permissions"""

    def setUp(self):
        super().setUp()

        # Add files with different permissions
        executable = rpmfluff.SourceFile(
            "executable", make_random_content(512).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/bin/%s-exec" % NAME, executable, mode="755")

        readonly = rpmfluff.SourceFile(
            "readonly.conf", make_random_content(256).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/etc/%s/readonly.conf" % NAME, readonly, mode="444"
        )

        normal = rpmfluff.SourceFile(
            "normal.txt", make_random_content(128).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/normal.txt" % NAME, normal, mode="644"
        )

    def runTest(self):
        # Build, extract, recreate, and re-extract
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)
        subprocess.run(
            [self.tarpm, "-x", "-f", original_rpm, "-O", extract_dir], check=True
        )

        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", recreated_rpm, extract_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        recreated_extract = os.path.join(self.output_dir, "recreated_extract")
        os.makedirs(recreated_extract)
        subprocess.run(
            [self.tarpm, "-x", "-f", recreated_rpm, "-O", recreated_extract], check=True
        )

        # Verify permissions
        payload_dir = os.path.join(recreated_extract, "payload")

        executable = os.path.join(payload_dir, "usr/bin", "%s-exec" % NAME)
        readonly = os.path.join(payload_dir, "etc", NAME, "readonly.conf")
        normal = os.path.join(payload_dir, "usr/share", NAME, "normal.txt")

        exec_mode = os.stat(executable).st_mode & 0o777
        readonly_mode = os.stat(readonly).st_mode & 0o777
        normal_mode = os.stat(normal).st_mode & 0o777

        self.assertEqual(exec_mode, 0o755)
        self.assertEqual(readonly_mode, 0o444)
        self.assertEqual(normal_mode, 0o644)


class TestCreateRPMWithEmptyFiles(TestUnpackRPM):
    """Test creating an RPM with empty files"""

    def setUp(self):
        super().setUp()

        # Add an empty file and a non-empty file
        empty = rpmfluff.SourceFile("empty.txt", b"")
        self.rpm.add_installed_file("/usr/share/%s/empty.txt" % NAME, empty)

        nonempty = rpmfluff.SourceFile(
            "nonempty.txt", make_random_content(100).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/%s/nonempty.txt" % NAME, nonempty)

    def runTest(self):
        # Build, extract, recreate, and re-extract
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)
        subprocess.run(
            [self.tarpm, "-x", "-f", original_rpm, "-O", extract_dir], check=True
        )

        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", recreated_rpm, extract_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        recreated_extract = os.path.join(self.output_dir, "recreated_extract")
        os.makedirs(recreated_extract)
        subprocess.run(
            [self.tarpm, "-x", "-f", recreated_rpm, "-O", recreated_extract], check=True
        )

        # Verify both files exist with correct sizes
        payload_dir = os.path.join(recreated_extract, "payload")

        empty_file = os.path.join(payload_dir, "usr/share", NAME, "empty.txt")
        nonempty_file = os.path.join(payload_dir, "usr/share", NAME, "nonempty.txt")

        self.assertTrue(os.path.isfile(empty_file))
        self.assertTrue(os.path.isfile(nonempty_file))

        self.assertEqual(os.path.getsize(empty_file), 0)
        self.assertEqual(os.path.getsize(nonempty_file), 100)


class TestCreateRPMMultipleFiles(TestUnpackRPM):
    """Test creating an RPM with many files"""

    def setUp(self):
        super().setUp()

        # Add multiple files
        for i in range(20):
            size = 100 * (i + 1)
            content = rpmfluff.SourceFile(
                "file%d.txt" % i, make_random_content(size).encode("utf-8")
            )
            self.rpm.add_installed_file("/usr/share/%s/file%d.txt" % (NAME, i), content)

    def runTest(self):
        # Build, extract, recreate, and re-extract
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        extract_dir = os.path.join(self.output_dir, "extracted")
        os.makedirs(extract_dir)
        subprocess.run(
            [self.tarpm, "-x", "-f", original_rpm, "-O", extract_dir], check=True
        )

        recreated_rpm = os.path.join(self.output_dir, "recreated.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", recreated_rpm, extract_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, f"Create failed: {err.decode()}")

        recreated_extract = os.path.join(self.output_dir, "recreated_extract")
        os.makedirs(recreated_extract)
        subprocess.run(
            [self.tarpm, "-x", "-f", recreated_rpm, "-O", recreated_extract], check=True
        )

        # Verify all files were recreated with correct sizes
        payload_dir = os.path.join(recreated_extract, "payload")

        for i in range(20):
            filepath = os.path.join(payload_dir, "usr/share", NAME, "file%d.txt" % i)
            self.assertTrue(os.path.isfile(filepath))
            self.assertEqual(os.path.getsize(filepath), 100 * (i + 1))


# The three tags a user has to supply, plus the two queries expect
MINIMAL_TAGS = [
    {"tag": "Name", "type": "string", "value": "hello"},
    {"tag": "Version", "type": "string", "value": "1.0"},
    {"tag": "Release", "type": "string", "value": "1"},
    {"tag": "Summary", "type": "i18n string", "value": "A greeting"},
    {"tag": "License", "type": "string", "value": "MIT"},
]


def run_tarpm(args):
    """Run tarpm with the given arguments and return (returncode, stdout, stderr)"""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate()

    return (proc.returncode, out.decode(), err.decode())


def query_rpm(pkg, queryformat):
    """Run rpm -qp over a package and return what it printed"""
    proc = subprocess.Popen(
        ["rpm", "-qp", "--qf", queryformat, pkg],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = proc.communicate()

    return out.decode()


def verify_rpm(pkg):
    """Run rpm -K over a package at the digest level and return (returncode, stderr)"""
    proc = subprocess.Popen(
        ["rpm", "--define", "_pkgverify_level digest", "-K", pkg],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = proc.communicate()

    return (proc.returncode, err.decode())


def write_minimal_tree(tree, tags=MINIMAL_TAGS):
    """Create a tree holding header.json and an empty payload subdirectory"""
    os.makedirs(os.path.join(tree, "payload"))

    f = open(os.path.join(tree, "header.json"), "w")
    json.dump({"tags": tags}, f)
    f.close()

    return tree


class CreateFromMinimalTree(RequiresTarpm):
    """A tree holding only header.json and a payload makes an installable package"""

    # the RPM format to write, overridden by the v6 subclass
    rpmformat = "4"

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        bindir = os.path.join(self.tree, "payload", "usr", "bin")
        os.makedirs(bindir)

        program = os.path.join(bindir, "hello")
        f = open(program, "w")
        f.write("#!/bin/sh\necho hello\n")
        f.close()
        os.chmod(program, 0o755)

    def runTest(self):
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", self.rpmformat, "-f", pkg, self.tree]
        )
        self.assertEqual(rc, 0, msg=err)
        self.assertTrue(os.path.isfile(pkg))

        # the defaults pass filled in everything rpm wants
        fields = query_rpm(
            pkg,
            "%{name}|%{version}|%{release}|%{os}|%{arch}|%{summary}"
            "|%{payloadformat}|%{sourcerpm}|%{buildhost}",
        ).split("|")

        self.assertEqual(fields[0], "hello")
        self.assertEqual(fields[1], "1.0")
        self.assertEqual(fields[2], "1")
        self.assertEqual(fields[3], "linux")
        self.assertEqual(fields[4], platform.machine())
        self.assertEqual(fields[5], "A greeting")
        self.assertEqual(fields[6], "cpio")
        self.assertEqual(fields[7], "(none)")
        self.assertNotEqual(fields[8], "(none)")

        # the payload scan found the program and the directories above it
        files = query_rpm(pkg, "[%{filenames}\n]").split()
        self.assertEqual(files, ["/usr", "/usr/bin", "/usr/bin/hello"])

        # the package provides itself at its own EVR
        provides = query_rpm(pkg, "[%{providename} %{provideversion}\n]").split("\n")
        self.assertIn("hello 1.0-1", provides)

        # the region trailer is there, so rpm does not call this a v3 package
        self.assertNotEqual(query_rpm(pkg, "%{headerimmutable:len}"), "(none)")

        rc, err = verify_rpm(pkg)
        self.assertEqual(rc, 0, msg=err)


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class CreateV6FromMinimalTree(CreateFromMinimalTree):
    """The same minimal tree written as an RPM v6 format package"""

    rpmformat = "6"


class CreateWithNoFiles(RequiresTarpm):
    """A tree with an empty payload makes a package carrying no file list tags"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))

    def runTest(self):
        formats = ["4", "6"] if HAVE_RPM_FORMAT_SIX else ["4"]

        for rpmformat in formats:
            pkg = os.path.join(self.output_dir, "hello-%s.rpm" % rpmformat)
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", rpmformat, "-f", pkg, self.tree]
            )
            self.assertEqual(rc, 0, msg=err)

            self.assertEqual(query_rpm(pkg, "%{name}"), "hello")
            self.assertEqual(query_rpm(pkg, "[%{filenames}\n]"), "")

            rc, err = verify_rpm(pkg)
            self.assertEqual(rc, 0, msg=err)


class CreateFailsWithoutInputDir(RequiresTarpm):
    """A missing input directory makes tarpm exit non-zero"""

    def runTest(self):
        pkg = os.path.join(self.output_dir, "hello.rpm")
        tree = os.path.join(self.output_dir, "missing")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

        self.assertNotEqual(rc, 0)
        self.assertIn("missing input directory", err)
        self.assertFalse(os.path.isfile(pkg))


@unittest.skipIf(os.geteuid() == 0, "root can read a directory with mode 000")
class CreateFailsOnUnreadableInputDir(RequiresTarpm):
    """An input directory tarpm cannot read makes it exit non-zero"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        os.chmod(self.tree, 0o000)

    def tearDown(self):
        os.chmod(self.tree, 0o755)
        super().tearDown()

    def runTest(self):
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, self.tree])

        self.assertNotEqual(rc, 0)
        self.assertIn("missing input directory", err)
        self.assertFalse(os.path.isfile(pkg))


class CreateRejectsHeaderWithoutNVR(RequiresTarpm):
    """A header naming none of Name, Version and Release is turned away"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(
            os.path.join(self.output_dir, "tree"), MINIMAL_TAGS[:2]
        )

    def runTest(self):
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, self.tree])

        self.assertNotEqual(rc, 0)
        self.assertIn("Release", err)
        self.assertFalse(os.path.isfile(pkg))


class CreateRejectsNonStringValue(RequiresTarpm):
    """A string tag holding a JSON number or boolean is turned away"""

    def runTest(self):
        cases = [
            ("Version", "string", 1234567890123456789),
            ("Summary", "i18n string", True),
        ]

        for i, (name, tagtype, value) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]

            for tag in tags:
                if tag["tag"] == name:
                    tag["type"] = tagtype
                    tag["value"] = value

            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn(
                "the %s tag has a string type but its value is not a string" % name,
                err,
            )
            self.assertFalse(os.path.isfile(pkg))


class CreateKeepsChangelogBlankLines(RequiresTarpm):
    """Blank lines at either end of a changelog entry survive a round trip"""

    def setUp(self):
        super().setUp()

        self.lines = ["", "- first", "", "", "- second", ""]
        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))

        path = os.path.join(self.tree, "header.json")
        f = open(path)
        header = json.load(f)
        f.close()

        header["changelog"] = [
            {
                "timestamp": "Tue Mar 15 2022",
                "name": "Some One <nobody@example.com> - 1.0-1",
                "text": self.lines,
            }
        ]

        f = open(path, "w")
        json.dump(header, f)
        f.close()

    def runTest(self):
        first = os.path.join(self.output_dir, "first.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", first, self.tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query_rpm(first, "%{changelogtext}"), "\n".join(self.lines))

        extracted = os.path.join(self.output_dir, "extracted")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", first, "-O", extracted])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(extracted, "header.json"))
        header = json.load(f)
        f.close()

        self.assertEqual(header["changelog"][0]["text"], self.lines)


class CreateRoundTripsEmptyFileTag(RequiresTarpm):
    """A file-backed tag holding "" extracts to an empty file and creates again"""

    def setUp(self):
        super().setUp()

        tags = MINIMAL_TAGS + [
            {"tag": "Description", "type": "i18n string", "value": ""},
            {"tag": "Postin", "type": "string", "value": ""},
        ]
        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)

    def runTest(self):
        first = os.path.join(self.output_dir, "first.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", first, self.tree])
        self.assertEqual(rc, 0, msg=err)

        extracted = os.path.join(self.output_dir, "extracted")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", first, "-O", extracted])
        self.assertEqual(rc, 0, msg=err)

        for name in ["description.txt", "postin.txt"]:
            path = os.path.join(extracted, name)
            self.assertTrue(os.path.isfile(path), msg=name)
            self.assertEqual(os.path.getsize(path), 0, msg=name)

        f = open(os.path.join(extracted, "header.json"))
        header = json.load(f)
        f.close()

        for tag in header["tags"]:
            if tag["tag"] in ["Description", "Postin"]:
                self.assertIn("file", tag, msg=tag["tag"])

        second = os.path.join(self.output_dir, "second.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", second, extracted])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query_rpm(second, "%{description}|%{postin}"), "|")


@unittest.skipUnless(os.path.exists("/dev/full"), "/dev/full is not available")
class CreateFailsOnWriteError(RequiresTarpm):
    """A package that cannot be written makes tarpm exit non-zero"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))

    def runTest(self):
        # every write to /dev/full fails with ENOSPC
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", "/dev/full", self.tree])

        self.assertNotEqual(rc, 0)
        self.assertIn("No space left on device", err)

        # the device is not a regular file, so it is left alone
        self.assertTrue(os.path.exists("/dev/full"))


@unittest.skipUnless(os.environ.get("FAILFWRITE"), "FAILFWRITE is not set")
class CreateFailsOnOneHeaderWriteError(RequiresTarpm):
    """One failed write in the signature header is not lost to the writes after it"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))

    def runTest(self):
        # call 1 writes the lead, 2 and 3 the signature intro and index
        for call in ["2", "3"]:
            pkg = os.path.join(self.output_dir, "hello-%s.rpm" % call)
            env = dict(os.environ)
            env["LD_PRELOAD"] = os.environ["FAILFWRITE"]
            env["FAIL_FWRITE"] = call

            proc = subprocess.Popen(
                [self.tarpm, "-c", "-f", pkg, self.tree],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env,
            )
            out, err = proc.communicate()

            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("fwrite", err.decode())
            self.assertFalse(os.path.exists(pkg))


@unittest.skipIf(os.geteuid() == 0, "root can read a file with mode 000")
class CreateFailsOnUnreadablePayloadFile(RequiresTarpm):
    """A payload file that cannot be read makes tarpm exit non-zero"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        bindir = os.path.join(self.tree, "payload", "usr", "bin")
        os.makedirs(bindir)

        self.program = os.path.join(bindir, "hello")
        f = open(self.program, "w")
        f.write("#!/bin/sh\necho hello\n")
        f.close()
        os.chmod(self.program, 0o000)

    def runTest(self):
        formats = ["4", "6"] if HAVE_RPM_FORMAT_SIX else ["4"]

        for rpmformat in formats:
            pkg = os.path.join(self.output_dir, "hello-%s.rpm" % rpmformat)
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", rpmformat, "-f", pkg, self.tree]
            )

            self.assertNotEqual(rc, 0)
            self.assertIn("failed to add file: /usr/bin/hello", err)
            self.assertFalse(os.path.exists(pkg))


class CreateWithEmptiedPayload(TestUnpackRPM):
    """An extracted tree with everything taken back out drops the file list tags"""

    def setUp(self):
        super().setUp()

        content = rpmfluff.SourceFile(
            "README", make_random_content(256).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, content)

    def runTest(self):
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        tree = os.path.join(self.output_dir, "extracted")
        os.makedirs(tree)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", original_rpm, "-O", tree])
        self.assertEqual(rc, 0, msg=err)

        # take everything back out of the payload tree
        payload = os.path.join(tree, "payload")

        for entry in os.listdir(payload):
            shutil.rmtree(os.path.join(payload, entry))

        pkg = os.path.join(self.output_dir, "recreated.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        self.assertEqual(query_rpm(pkg, "%{name}"), NAME)
        self.assertEqual(query_rpm(pkg, "[%{filenames}\n]"), "")
        self.assertEqual(query_rpm(pkg, "%{size}"), "0")

        rc, err = verify_rpm(pkg)
        self.assertEqual(rc, 0, msg=err)
