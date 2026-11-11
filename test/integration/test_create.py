#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import hashlib
import json
import os
import platform
import shutil
import stat
import struct
import subprocess
import unittest
import rpmfluff
from baseclass import NAME
from baseclass import PAYLOAD_DIGEST
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


def edit_header(path, func):
    """Load header.json, hand it to func to change, and write it back"""
    with open(path) as f:
        header = json.load(f)

    func(header)

    with open(path, "w") as f:
        json.dump(header, f)


def run_create(tarpm, rpm, tree):
    """Run tarpm -c and return the exit code and stderr"""
    proc = subprocess.Popen(
        [tarpm, "-c", "-f", rpm, tree],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = proc.communicate()
    return proc.returncode, err.decode("utf-8")


def set_description_file(header, name):
    """Point the Description tag at another file"""
    for tag in header["tags"]:
        if tag["tag"] == "Description":
            tag["file"] = name


class CreateRefusesPathOutsidePayload(TestUnpackRPM):
    """A files entry with ".." does not pull in a file outside the tree"""

    def runTest(self):
        super().runTest()

        # payload/../secret.txt is the extract directory itself
        with open(os.path.join(self.output_dir, "secret.txt"), "w") as f:
            f.write("not for the package\n")

        edit_header(
            self.header,
            lambda h: h.setdefault("files", []).append(
                {"path": "/../secret.txt", "type": "file", "mode": "0644"}
            ),
        )

        rpm = os.path.join(self.output_dir, "out.rpm")
        rc, err = run_create(self.tarpm, rpm, self.output_dir)
        self.assertEqual(rc, 1, msg=err)
        self.assertIn("/../secret.txt leaves the payload tree", err)
        self.assertFalse(os.path.exists(rpm))


class CreateRefusesSymlinkedPayloadDir(TestUnpackRPM):
    """A symlinked directory in the payload tree is not followed"""

    def runTest(self):
        super().runTest()

        outside = os.path.join(self.output_dir, "outside")
        os.makedirs(outside)

        with open(os.path.join(outside, "secret.txt"), "w") as f:
            f.write("not for the package\n")

        os.symlink(outside, os.path.join(self.output_dir, "payload", "evil"))

        edit_header(
            self.header,
            lambda h: h.setdefault("files", []).append(
                {"path": "/evil/secret.txt", "type": "file", "mode": "0644"}
            ),
        )

        rpm = os.path.join(self.output_dir, "out.rpm")
        rc, err = run_create(self.tarpm, rpm, self.output_dir)
        self.assertEqual(rc, 1, msg=err)
        self.assertIn("/evil/secret.txt leaves the payload tree", err)
        self.assertFalse(os.path.exists(rpm))


class CreateRefusesTagFileOutsideTree(TestUnpackRPM):
    """The file key of a tag cannot name a file outside the tree"""

    def runTest(self):
        super().runTest()

        secret = os.path.join(os.path.dirname(self.output_dir), "secret.txt")
        rpm = os.path.join(self.output_dir, "out.rpm")

        for name in [secret, "../secret.txt", "sub/../../secret.txt"]:
            edit_header(self.header, lambda h: set_description_file(h, name))
            rc, err = run_create(self.tarpm, rpm, self.output_dir)
            self.assertEqual(rc, 1, msg=err)
            self.assertIn('names "%s", which is not a file below' % name, err)
            self.assertFalse(os.path.exists(rpm))


class CreateRefusesSymlinkedTagFile(TestUnpackRPM):
    """The file key of a tag cannot name a symlink"""

    def runTest(self):
        super().runTest()

        os.symlink("/etc/passwd", os.path.join(self.output_dir, "link.txt"))
        edit_header(self.header, lambda h: set_description_file(h, "link.txt"))

        rpm = os.path.join(self.output_dir, "out.rpm")
        rc, err = run_create(self.tarpm, rpm, self.output_dir)
        self.assertEqual(rc, 1, msg=err)
        self.assertIn('names "link.txt", which is not a file below', err)


class CreateReadsTagFileInSeparatePayload(TestUnpackRPM):
    """The file key of a tag may name a file in a payload directory elsewhere"""

    def runTest(self):
        super().runTest()

        # header.json goes in meta/ and the payload in pl/, apart from each other
        meta = os.path.join(self.output_dir, "meta")
        pl = os.path.join(self.output_dir, "pl")
        os.makedirs(meta)
        os.makedirs(pl)
        header = os.path.join(meta, "header.json")
        shutil.move(self.header, header)
        shutil.move(
            os.path.join(self.output_dir, "description.txt"),
            os.path.join(pl, "description.txt"),
        )

        rpm = os.path.join(self.output_dir, "out.rpm")
        args = [self.tarpm, "-c", "-f", rpm, "-H", header, "-P", pl, self.output_dir]

        # relative to header.json and absolute both reach the payload directory
        for name in ["../pl/description.txt", os.path.join(pl, "description.txt")]:
            edit_header(header, lambda h: set_description_file(h, name))
            proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(proc.returncode, 0, msg=proc.stderr.decode("utf-8"))
            self.assertTrue(os.path.isfile(rpm))
            os.unlink(rpm)

        # the directory above both is neither of them
        edit_header(header, lambda h: set_description_file(h, "../lead.json"))
        proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(proc.returncode, 1)
        self.assertIn(
            'names "../lead.json", which is not a file below',
            proc.stderr.decode("utf-8"),
        )


class CreateReadsTagFileInSubdirectory(TestUnpackRPM):
    """The file key of a tag may name a file in a subdirectory"""

    def runTest(self):
        super().runTest()

        os.makedirs(os.path.join(self.output_dir, "sub"))
        shutil.copy(
            os.path.join(self.output_dir, "description.txt"),
            os.path.join(self.output_dir, "sub", "description.txt"),
        )
        os.unlink(os.path.join(self.output_dir, "description.txt"))
        edit_header(
            self.header, lambda h: set_description_file(h, "sub/description.txt")
        )

        rpm = os.path.join(self.output_dir, "out.rpm")
        rc, err = run_create(self.tarpm, rpm, self.output_dir)
        self.assertEqual(rc, 0, msg=err)
        self.assertTrue(os.path.isfile(rpm))


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


class CreateFromRawChangelogTags(RequiresTarpm):
    """Raw changelog tags go in when they agree and are refused otherwise"""

    def tree(self, name, names):
        tags = MINIMAL_TAGS + [
            {"tag": "Changelogtime", "type": "int32", "value": [1700000000]},
            {"tag": "Changelogname", "type": "string array", "value": names},
            {"tag": "Changelogtext", "type": "string array", "value": ["- text"]},
        ]

        return write_minimal_tree(os.path.join(self.output_dir, name), tags)

    def runTest(self):
        pkg = os.path.join(self.output_dir, "good.rpm")
        tree = self.tree("good", ["A B <a@b.c>"])
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query_rpm(pkg, "%{changelogtext}"), "- text")

        pkg = os.path.join(self.output_dir, "bad.rpm")
        tree = self.tree("bad", ["A B <a@b.c>", "C D <c@d.e>"])
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertNotEqual(rc, 0)
        self.assertIn("changelog tags in header.json have mismatched lengths", err)
        self.assertFalse(os.path.exists(pkg))


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
        if HAVE_RPM_FORMAT_SIX:
            formats = ["4", "6"]
        else:
            formats = ["4"]

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


class CreateRejectsBadRegionTags(RequiresTarpm):
    """A second region tag, the wrong region tag, or a bad region value is turned away"""

    def runTest(self):
        # a region trailer the way rpm writes it, as base64
        region = "AAAAPwAAAAcAAAAAAAAAEA=="

        cases = [
            (
                [
                    {"tag": "Headerimmutable", "type": "binary blob", "value": region},
                    {
                        "tag": "Headerimmutable",
                        "type": "binary blob",
                        "value": "A" * 400,
                    },
                ],
                "carries 2 region tags, only one is allowed",
            ),
            (
                [{"tag": "Headersignatures", "type": "binary blob", "value": region}],
                "the Headersignatures tag does not belong in this header",
            ),
            (
                [{"tag": "Headerimmutable", "type": "string", "value": region}],
                "the Headerimmutable tag must have type binary blob and a value of 16 bytes",
            ),
            (
                [
                    {
                        "tag": "Headerimmutable",
                        "type": "binary blob",
                        "value": "AAAAPwAAAAc=",
                    }
                ],
                "the Headerimmutable tag must have type binary blob and a value of 16 bytes",
            ),
            (
                [{"tag": "Headerimmutable", "type": "binary blob", "value": "A" * 64}],
                "the Headerimmutable tag must have type binary blob and a value of 16 bytes",
            ),
            (
                [{"tag": "Headerimmutable", "type": "binary blob", "value": 7}],
                "the Headerimmutable tag must have type binary blob and a value of 16 bytes",
            ),
        ]

        for i, (extra, message) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS] + extra
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn(message, err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsWrongValueType(RequiresTarpm):
    """A tag value whose JSON type does not fit the tag type is turned away"""

    def runTest(self):
        cases = [
            ("Platform", "string array", "linux", "is not an array of strings"),
            ("Platform", "string array", ["linux", None], "is not an array of strings"),
            ("Epoch", "int32", True, "is not a number or an array of numbers"),
            ("Epoch", "int32", [1, "x"], "is not a number or an array of numbers"),
            ("Epoch", "int32", "12x", "is not a number or an array of numbers"),
            ("Cookie", "binary blob", 7, "is not base64 data"),
            ("Cookie", "binary blob", "!!!", "is not base64 data"),
        ]

        for i, (name, tagtype, value, message) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": name, "type": tagtype, "value": value})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn("the %s tag has a" % name, err)
            self.assertIn(message, err)
            self.assertNotIn("Assertion", err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsUnwritableTypes(RequiresTarpm):
    """A tag type tarpm cannot write is turned away and not written as type 0"""

    def runTest(self):
        cases = [
            ("(null)", "(null)", "has no type tarpm can write"),
            ("(unknown)", "(unknown)", "has no type tarpm can write"),
            ("strng", "linux", "has no type tarpm can write"),
            ("char", "", "has a char type but its value is not a string"),
            ("char", 7, "has a char type but its value is not a string"),
        ]

        for i, (tagtype, value, message) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": "Platform", "type": tagtype, "value": value})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0, msg=tagtype)
            self.assertIn("the Platform tag %s" % message, err)
            self.assertFalse(os.path.isfile(pkg))


class CreateCharTagRoundTrip(RequiresTarpm):
    """A char tag keeps every character it carries"""

    def runTest(self):
        # rpm wants a known tag to have its own type, so we use a number
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "#5999", "type": "char", "value": "abc"})
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        again = os.path.join(self.output_dir, "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(again, "header.json"))
        header = json.load(f)
        f.close()

        chars = [t for t in header["tags"] if t["tag"] == "#5999"]
        self.assertEqual(len(chars), 1)
        self.assertEqual(chars[0]["type"], "char")
        self.assertEqual(chars[0]["value"], "abc")


class CreateLongName(RequiresTarpm):
    """A Name too long for a memfd_create() name still makes a package"""

    def runTest(self):
        name = "x" * 300
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags[0]["value"] = name
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)
        pkg = os.path.join(self.output_dir, "long.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query_rpm(pkg, "%{name}"), name)


class CreateRejectsUnknownTagName(RequiresTarpm):
    """A tag name tarpm does not know is turned away and not written as tag 0"""

    def runTest(self):
        cases = [
            {"tag": "Nmae", "type": "string", "value": "hello"},
            {"tag": "Md5", "type": "binary blob", "value": "AAAA"},
            {"tag": "#0", "type": "string", "value": "hello"},
        ]

        for i, extra in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS] + [extra]
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn('unknown tag "%s" in header data' % extra["tag"], err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsEmptyArray(RequiresTarpm):
    """A tag holding an empty array is turned away and not written with a count of 0"""

    def runTest(self):
        cases = [
            ("Excludearch", "string array"),
            ("Epoch", "int32"),
            ("#5999", "int16"),
        ]

        for i, (name, tagtype) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": name, "type": tagtype, "value": []})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0, msg=name)
            self.assertIn("tag has an empty array as its value", err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsNumberOutOfRange(RequiresTarpm):
    """A number too big or too small for its type is turned away"""

    def runTest(self):
        cases = [
            ("Epoch", "int32", 4294967297),
            ("Epoch", "int32", "4294967296"),
            ("#5999", "int32", -2147483649),
            ("#5999", "int32", [1, 4294967296]),
            ("#5999", "int16", 65536),
            ("#5999", "int16", -32769),
            ("#5999", "int8", 256),
            ("#5999", "int8", -129),
        ]

        for i, (name, tagtype, value) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": name, "type": tagtype, "value": value})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0, msg="%s %r" % (name, value))
            self.assertIn("does not fit in its %s type" % tagtype, err)
            self.assertFalse(os.path.isfile(pkg))


class CreateAcceptsNumberAtTypeLimits(RequiresTarpm):
    """The ends of each number type's range still go in"""

    def runTest(self):
        cases = [
            ("int32", [4294967295, -2147483648]),
            ("int16", [65535, -32768]),
            ("int8", [255, -128]),
        ]

        for i, (tagtype, value) in enumerate(cases):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": "#5999", "type": tagtype, "value": value})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertEqual(rc, 0, msg=err)
            self.assertNotIn("does not fit", err)


class CreateRejectsTagEntryNotAnObject(RequiresTarpm):
    """A null or other non-object in the tags array is named and refused"""

    def runTest(self):
        for i, bad in enumerate([None, "Name"]):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.insert(1, bad)
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0, msg=repr(bad))
            self.assertIn("entry 1 of the tags in header.json is not an object", err)
            self.assertNotIn("missing Name, Version, or Release", err)
            self.assertFalse(os.path.isfile(pkg))


class CreateReadsTagNameInAnyCase(RequiresTarpm):
    """A tag named in another case is the tag the defaults and the payload look for"""

    def runTest(self):
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "PAYLOADCOMPRESSOR", "type": "string", "value": "xz"})
        tags.append({"tag": "buildhost", "type": "string", "value": "example"})
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertNotIn("more than once", err)

        self.assertEqual(query_rpm(pkg, "%{payloadcompressor}"), "xz")
        self.assertEqual(query_rpm(pkg, "%{buildhost}"), "example")

        rc, err = verify_rpm(pkg)
        self.assertEqual(rc, 0, msg=err)

        # the defaults did not add a second copy of either tag
        again = os.path.join(self.output_dir, "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(again, "header.json"))
        header = json.load(f)
        f.close()

        names = [t["tag"] for t in header["tags"]]
        self.assertEqual(names.count("Payloadcompressor"), 1)
        self.assertEqual(names.count("Buildhost"), 1)


class CreateWarnsAboutDuplicateTag(RequiresTarpm):
    """A tag header.json carries twice gets one warning and still makes a package"""

    def runTest(self):
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "Url", "type": "string", "value": "https://a.example"})
        tags.append({"tag": "URL", "type": "string", "value": "https://b.example"})
        tags.append({"tag": "#1020", "type": "string", "value": "https://c.example"})
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(
            err.count("Url is in the tags in header.json more than once"), 1, msg=err
        )
        self.assertTrue(os.path.isfile(pkg))


def query_locale(pkg, queryformat, language):
    """Run rpm -qp over a package with LANGUAGE set and return what it printed"""
    proc = subprocess.Popen(
        ["rpm", "-qp", "--qf", queryformat, pkg],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=dict(os.environ, LANGUAGE=language),
    )
    out, err = proc.communicate()

    return out.decode()


class TranslatedTree(RequiresTarpm):
    """Class for tests of the translations of an i18n string tag"""

    def tree(self, name, tags, files={}):
        """Write a tree holding tags and text files beside header.json"""
        tree = write_minimal_tree(os.path.join(self.output_dir, name), tags)

        for filename, text in files.items():
            f = open(os.path.join(tree, filename), "w")
            f.write(text)
            f.close()

        return tree

    def tags(self, *extra):
        """Return the minimal tags without Summary, plus the given ones"""
        tags = [dict(t) for t in MINIMAL_TAGS if t["tag"] != "Summary"]

        return tags + list(extra)


LOCALES = {
    "tag": "Headeri18ntable",
    "type": "string array",
    "value": ["C", "de", "fr"],
}


class CreateTranslatedStrings(TranslatedTree):
    """Translations in header.json and in files reach the package"""

    def runTest(self):
        tags = self.tags(
            LOCALES,
            {
                "tag": "Summary",
                "type": "i18n string",
                "value": {"C": "hello", "de": "hallo", "fr": "salut"},
            },
            {
                "tag": "Description",
                "type": "i18n string",
                "file": {"C": "description.txt", "fr": "description.fr.txt"},
            },
        )
        tree = self.tree(
            "tree",
            tags,
            {"description.txt": "text", "description.fr.txt": "texte"},
        )
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        for language, expect in [
            ("C", "hello|text"),
            ("de", "hallo|"),
            ("fr", "salut|texte"),
        ]:
            self.assertEqual(
                query_locale(pkg, "%{summary}|%{description}", language), expect
            )

        # the locale with no string stays a gap so fr lines up with the table
        again = os.path.join(self.output_dir, "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(again, "header.json"))
        header = json.load(f)
        f.close()

        tags = dict((t["tag"], t) for t in header["tags"])
        self.assertEqual(
            tags["Summary"]["value"], {"C": "hello", "de": "hallo", "fr": "salut"}
        )
        self.assertEqual(
            tags["Description"]["file"],
            {
                "C": "description.txt",
                "de": "description.de.txt",
                "fr": "description.fr.txt",
            },
        )

        f = open(os.path.join(again, "description.fr.txt"))
        self.assertEqual(f.read(), "texte")
        f.close()

        self.assertEqual(os.path.getsize(os.path.join(again, "description.de.txt")), 0)


class CreateRejectsBadTranslations(TranslatedTree):
    """Translations with no place in Headeri18ntable are turned away"""

    def runTest(self):
        cases = [
            ([LOCALES], {"C": "a", "es": "b"}, 'names the locale "es"'),
            ([], {"C": "a"}, "has no Headeri18ntable tag"),
            ([LOCALES], {}, "has no strings"),
            ([LOCALES], {"C": 7}, 'value for locale "C" that is not a string'),
            ([LOCALES], {"C": None}, 'value for locale "C" that is not a string'),
        ]

        for i, (table, value, message) in enumerate(cases):
            summary = {"tag": "Summary", "type": "i18n string", "value": value}
            tree = self.tree("tree%d" % i, self.tags(*(table + [summary])))
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn(message, err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsTranslationOutsideTree(TranslatedTree):
    """A translation file gets the same path checks as any other tag file"""

    def runTest(self):
        secret = os.path.join(self.output_dir, "secret")
        f = open(secret, "w")
        f.write("private\n")
        f.close()

        description = {
            "tag": "Description",
            "type": "i18n string",
            "file": {"C": "description.txt", "de": "../secret"},
        }
        tree = self.tree(
            "tree", self.tags(LOCALES, description), {"description.txt": "text"}
        )
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

        self.assertNotEqual(rc, 0)
        self.assertIn("which is not a file below", err)
        self.assertFalse(os.path.isfile(pkg))


class ExtractRejectsUnsafeLocale(TranslatedTree):
    """A locale that cannot be part of a file name stops the extract"""

    def runTest(self):
        table = {
            "tag": "Headeri18ntable",
            "type": "string array",
            "value": ["C", "../../../evil"],
        }
        description = {
            "tag": "Description",
            "type": "i18n string",
            "value": {"C": "text", "../../../evil": "bad"},
        }
        tree = self.tree("tree", self.tags(table, description))
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        before = set(os.listdir(self.output_dir))
        again = os.path.join(self.output_dir, "deep", "er", "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])

        self.assertNotEqual(rc, 0)
        self.assertIn("cannot be used in a file name", err)

        for dirpath, dirnames, filenames in os.walk(self.output_dir):
            self.assertFalse([f for f in filenames if f.endswith("evil.txt")])

        self.assertLessEqual(set(os.listdir(self.output_dir)) - before, {"deep"})


class ExtractWarnsAboutMissingLocaleTable(TranslatedTree):
    """Translations with no Headeri18ntable keep the first string"""

    def runTest(self):
        summary = {
            "tag": "Summary",
            "type": "i18n string",
            "value": {"C": "hello", "de": "hallo"},
        }
        tree = self.tree("tree", self.tags(LOCALES, summary))
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # turn the table in to a tag nothing reads
        f = open(pkg, "rb")
        data = bytearray(f.read())
        f.close()

        nentries, nbytes = struct.unpack_from(">II", data, 96 + 8)
        hlen = 16 * nentries + nbytes
        hdr = 96 + 16 + hlen + (8 - (hlen % 8)) % 8
        nentries, nbytes = struct.unpack_from(">II", data, hdr + 8)
        found = False

        for i in range(nentries):
            entry = hdr + 16 + 16 * i

            if struct.unpack_from(">I", data, entry)[0] == 100:
                struct.pack_into(">I", data, entry, 101)
                found = True

        self.assertTrue(found)

        f = open(pkg, "wb")
        f.write(data)
        f.close()

        again = os.path.join(self.output_dir, "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])
        self.assertEqual(rc, 0, msg=err)
        self.assertIn("keeping the first string", err)

        f = open(os.path.join(again, "header.json"))
        header = json.load(f)
        f.close()

        tags = dict((t["tag"], t.get("value")) for t in header["tags"])
        self.assertEqual(tags["Summary"], "hello")


class CreateRejectsUnknownCompressor(RequiresTarpm):
    """A Payloadcompressor tarpm does not know is turned away"""

    def runTest(self):
        for i, name in enumerate(["zx", "lz4", "ufdio", ""]):
            tags = [dict(t) for t in MINIMAL_TAGS]
            tags.append({"tag": "Payloadcompressor", "type": "string", "value": name})
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i), tags)
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)

            # a package already there stays when the name is refused
            f = open(pkg, "w")
            f.write("keep me\n")
            f.close()

            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn('unknown Payloadcompressor "%s"' % name, err)

            f = open(pkg)
            self.assertEqual(f.read(), "keep me\n")
            f.close()


class CreatePayloadflags(RequiresTarpm):
    """Payloadflags must be a level with an optional thread count"""

    def tree_with(self, name, compressor, flags):
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "Payloadcompressor", "type": "string", "value": compressor})
        tags.append({"tag": "Payloadflags", "type": "string", "value": flags})
        tree = write_minimal_tree(os.path.join(self.output_dir, name), tags)

        f = open(os.path.join(tree, "payload", "README"), "w")
        f.write("hello\n")
        f.close()

        return tree

    def runTest(self):
        # what rpmbuild writes, with and without a thread count
        good = [
            ("xz", "3T"),
            ("xz", "2T16"),
            ("gzip", "9"),
            ("zstd", "19T8"),
            ("zstd", "0"),
        ]

        for i, (compressor, flags) in enumerate(good):
            tree = self.tree_with("good%d" % i, compressor, flags)
            pkg = os.path.join(self.output_dir, "good%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", pkg, tree])
            self.assertEqual(rc, 0, msg=err)
            rc, err = verify_rpm(pkg)
            self.assertEqual(rc, 0, msg=err)

            # the payload still unpacks
            rc, out, err = run_tarpm([self.tarpm, "-t", "-f", pkg])
            self.assertEqual(rc, 0, msg=err)
            self.assertIn("./README", out)

        # extra libarchive options and junk are refused
        bad = [("xz", "6,xz:threads=64"), ("gzip", "fast"), ("zstd", "19x"), ("xz", "")]

        for i, (compressor, flags) in enumerate(bad):
            tree = self.tree_with("bad%d" % i, compressor, flags)
            pkg = os.path.join(self.output_dir, "bad%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", pkg, tree])
            self.assertNotEqual(rc, 0, msg=flags)
            self.assertIn('Payloadflags "%s" is not a compression level' % flags, err)
            self.assertFalse(os.path.exists(pkg))

        # a level the compressor does not take is an error too
        tree = self.tree_with("range", "gzip", "99")
        pkg = os.path.join(self.output_dir, "range.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", pkg, tree])
        self.assertNotEqual(rc, 0)
        self.assertIn("archive_write_set_options", err)
        self.assertFalse(os.path.exists(pkg))

        # we compress xz ourselves, so its levels get the same check
        tree = self.tree_with("xzrange", "xz", "10")
        pkg = os.path.join(self.output_dir, "xzrange.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", pkg, tree])
        self.assertNotEqual(rc, 0)
        self.assertIn('Payloadflags "10" is not a compression level', err)
        self.assertFalse(os.path.exists(pkg))


class CreateChangelogWithNoValidEntries(RequiresTarpm):
    """A changelog with no usable entry leaves the changelog tags out"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        changelog = [{"timestamp": "Wed Jul 29 2020", "text": ["- no name"]}]

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "changelog": changelog}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertIn("invalid changelog entry at index 0", err)

        # rpm reads the package and finds no changelog in it
        self.assertEqual(query_rpm(pkg, "%{name}"), "hello")
        self.assertEqual(query_rpm(pkg, "%{changelogtime}"), "(none)")


class CreateWarnsAboutUnknownDependencyType(RequiresTarpm):
    """A dependency type tarpm does not write gets a warning"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        dependencies = {
            "requires": [{"name": "bash"}],
            "requirez": [{"name": "typo"}],
        }

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "dependencies": dependencies}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(
            err.count('unknown dependency type "requirez" in dependencies'), 1, err
        )
        self.assertIn("bash", query_rpm(pkg, "[%{requirename}\n]"))
        self.assertNotIn("typo", query_rpm(pkg, "[%{requirename}\n]"))


class CreateBuildsFileListOnce(RequiresTarpm):
    """The file list tags are built once, so their warnings print once"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        sharedir = os.path.join(tree, "payload", "usr", "share")
        os.makedirs(sharedir)

        f = open(os.path.join(sharedir, "one"), "w")
        f.write("one\n")
        f.close()

        files = [{"path": "/usr/share/one", "type": "file", "verifyflags": ["bogus"]}]

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "files": files}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(err.count("unknown file verify flag: bogus"), 1, err)


class CreateRefusesWideRdev(RequiresTarpm):
    """A device number that does not fit in Filerdevs is an error"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        files = [
            {"path": "/dev/wide", "type": "chardev", "rdev": 70000, "flags": ["ghost"]}
        ]

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "files": files}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertNotEqual(rc, 0)
        self.assertIn("/dev/wide has device number 70000", err)
        self.assertFalse(os.path.exists(pkg))


class CreateKeepsWideInodesApart(RequiresTarpm):
    """Inode keys that only differ above 32 bits are not hard links"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        sharedir = os.path.join(tree, "payload", "usr", "share")
        os.makedirs(sharedir)

        for name in ["one", "two"]:
            f = open(os.path.join(sharedir, name), "w")
            f.write("%s\n" % name)
            f.close()

        files = [
            {"path": "/usr/share/one", "type": "file", "inode": 1},
            {"path": "/usr/share/two", "type": "file", "inode": (1 << 32) + 1},
        ]

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "files": files}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        inodes = query_rpm(pkg, "[%{fileinodes}\n]").split()
        self.assertEqual(len(set(inodes)), 2, inodes)

        # both files keep their own bytes in the payload
        again = os.path.join(self.output_dir, "again")
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", again])
        self.assertEqual(rc, 0, msg=err)

        for name in ["one", "two"]:
            f = open(os.path.join(again, "payload", "usr", "share", name))
            self.assertEqual(f.read(), "%s\n" % name)
            f.close()


class CreateSurvivesNullStrings(RequiresTarpm):
    """A JSON null where header.json wants a string does not crash create"""

    def tree_with(self, name, tags, files):
        tree = write_minimal_tree(os.path.join(self.output_dir, name), tags)
        os.makedirs(os.path.join(tree, "payload", "usr"))

        f = open(os.path.join(tree, "payload", "usr", "README"), "w")
        f.write("hello\n")
        f.close()

        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": tags, "files": files}, f)
        f.close()

        return tree

    def runTest(self):
        readme = {"path": "/usr/README", "type": "file"}

        # a null tag name is an unknown tag
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": None, "type": "string", "value": "x"})
        tree = self.tree_with("tag", tags, [readme])
        pkg = os.path.join(self.output_dir, "tag.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 1, msg=err)
        self.assertIn('unknown tag ""', err)

        # a null type is a mismatch the payload settles
        tree = self.tree_with(
            "type", MINIMAL_TAGS, [{"path": "/usr/README", "type": None}]
        )
        pkg = os.path.join(self.output_dir, "type.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertIn("/usr/README", query_rpm(pkg, "[%{filenames}\n]"))

        # an entry with a null path names nothing, so the file is new
        tree = self.tree_with("path", MINIMAL_TAGS, [{"path": None, "type": "file"}])
        pkg = os.path.join(self.output_dir, "path.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertIn("/usr/README is new in the payload", err)


class CreateAcceptsCompressorNone(RequiresTarpm):
    """A Payloadcompressor of none leaves the tag off an uncompressed package"""

    def runTest(self):
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "Payloadcompressor", "type": "string", "value": "none"})
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)

        f = open(os.path.join(tree, "payload", "README"), "w")
        f.write("hello\n")
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(
            query_rpm(pkg, "%{payloadcompressor}|%{payloadflags}"), "(none)|0"
        )

        # the cpio archive sits in the package as it is
        f = open(pkg, "rb")
        data = f.read()
        f.close()

        self.assertIn(b"070701", data)


class CreateAcceptsNumberString(RequiresTarpm):
    """A number tag may hold a string of digits"""

    def runTest(self):
        tags = [dict(t) for t in MINIMAL_TAGS]
        tags.append({"tag": "Epoch", "type": "int32", "value": "3"})
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"), tags)
        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query_rpm(pkg, "%{epoch}"), "3")


class CreateRejectsBadDependencies(RequiresTarpm):
    """A dependencies object of the wrong shape is turned away"""

    def runTest(self):
        cases = [
            (["provides"], "dependencies must be an object"),
            ({"provides": "hello"}, "provides in dependencies must be an array"),
            ({"requires": [1]}, "entry 0 in requires must be an object"),
            ({"requires": [{"name": None}]}, "entry 0 in requires has a name"),
            (
                {"requires": [{"name": "sh", "version": 1}]},
                "entry 0 in requires has a name",
            ),
            (
                {"requires": [{"name": "sh", "sense_flags": "pre"}]},
                "entry 0 in requires has sense_flags that is not an array of strings",
            ),
            (
                {"requires": [{"name": "sh", "sense_flags": [None]}]},
                "entry 0 in requires has sense_flags that is not an array of strings",
            ),
        ]

        for i, (dependencies, message) in enumerate(cases):
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i))
            edit_header(
                os.path.join(tree, "header.json"),
                lambda h: h.update({"dependencies": dependencies}),
            )
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn(message, err)
            self.assertNotIn("Assertion", err)
            self.assertFalse(os.path.isfile(pkg))


class CreateRejectsBadChangelogText(RequiresTarpm):
    """Changelog text lines that are not strings are turned away"""

    def runTest(self):
        for i, text in enumerate([[None], ["one", 2]]):
            tree = write_minimal_tree(os.path.join(self.output_dir, "tree%d" % i))
            changelog = [
                {
                    "timestamp": "Tue Mar 15 2022",
                    "name": "Some One <nobody@example.com> - 1.0-1",
                    "text": text,
                }
            ]
            edit_header(
                os.path.join(tree, "header.json"),
                lambda h: h.update({"changelog": changelog}),
            )
            pkg = os.path.join(self.output_dir, "hello%d.rpm" % i)
            rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])

            self.assertNotEqual(rc, 0)
            self.assertIn(
                "changelog entry 0 has text that is not an array of strings", err
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


@unittest.skipUnless(os.environ.get("FAILFWRITE"), "FAILFWRITE is not set")
class CreateFailureKeepsExistingPackage(RequiresTarpm):
    """A failed write leaves the package that was there and no temporary file"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        outdir = os.path.join(self.output_dir, "out")
        os.makedirs(outdir)

        pkg = os.path.join(outdir, "hello.rpm")
        f = open(pkg, "w")
        f.write("old package")
        f.close()

        env = dict(os.environ)
        env["LD_PRELOAD"] = os.environ["FAILFWRITE"]
        env["FAIL_FWRITE"] = "1"

        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", pkg, tree],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        out, err = proc.communicate()
        self.assertNotEqual(proc.returncode, 0)

        f = open(pkg)
        self.assertEqual(f.read(), "old package")
        f.close()

        self.assertEqual(os.listdir(outdir), ["hello.rpm"])


class CreateReplacesExistingPackage(RequiresTarpm):
    """A new package takes over the mode of the old one and replaces a symlink"""

    def setUp(self):
        super().setUp()

        self.tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        self.outdir = os.path.join(self.output_dir, "out")
        os.makedirs(self.outdir)

    def create(self, pkg):
        return run_tarpm([self.tarpm, "-c", "-f", pkg, self.tree])

    def runTest(self):
        # a new file gets the usual mode, not the 0600 of mkstemp()
        pkg = os.path.join(self.outdir, "hello.rpm")
        mask = os.umask(0o022)

        try:
            rc, out, err = self.create(pkg)
        finally:
            os.umask(mask)

        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(stat.S_IMODE(os.stat(pkg).st_mode), 0o644)
        self.assertEqual(query_rpm(pkg, "%{name}"), "hello")

        # an existing file keeps its mode
        os.chmod(pkg, 0o640)
        rc, out, err = self.create(pkg)
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(stat.S_IMODE(os.stat(pkg).st_mode), 0o640)

        # a symlink is replaced and what it points to is left alone
        target = os.path.join(self.output_dir, "target")
        f = open(target, "w")
        f.write("keep me")
        f.close()

        link = os.path.join(self.outdir, "link.rpm")
        os.symlink(target, link)
        rc, out, err = self.create(link)
        self.assertEqual(rc, 0, msg=err)
        self.assertFalse(os.path.islink(link))

        f = open(target)
        self.assertEqual(f.read(), "keep me")
        f.close()

        self.assertEqual(sorted(os.listdir(self.outdir)), ["hello.rpm", "link.rpm"])


@unittest.skipIf(os.geteuid() == 0, "root can write to a file with mode 444")
class CreateRefusesReadOnlyPackage(RequiresTarpm):
    """A package we cannot write to is not replaced"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        pkg = os.path.join(self.output_dir, "hello.rpm")
        f = open(pkg, "w")
        f.write("old package")
        f.close()
        os.chmod(pkg, 0o444)

        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertNotEqual(rc, 0)

        f = open(pkg)
        self.assertEqual(f.read(), "old package")
        f.close()


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
        if HAVE_RPM_FORMAT_SIX:
            formats = ["4", "6"]
        else:
            formats = ["4"]

        for rpmformat in formats:
            pkg = os.path.join(self.output_dir, "hello-%s.rpm" % rpmformat)
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", rpmformat, "-f", pkg, self.tree]
            )

            self.assertNotEqual(rc, 0)
            self.assertIn("failed to add file: /usr/bin/hello", err)
            self.assertFalse(os.path.exists(pkg))


class CreateFailsOnUnreadableDigest(RequiresTarpm):
    """A file list digest that cannot be recomputed is an error"""

    def runTest(self):
        tree = write_minimal_tree(os.path.join(self.output_dir, "tree"))
        bindir = os.path.join(tree, "payload", "usr", "bin")
        os.makedirs(bindir)

        program = os.path.join(bindir, "hello")
        f = open(program, "w")
        f.write("#!/bin/sh\necho hello\n")
        f.close()
        os.chmod(program, 0o000)

        # the entry carries a digest that is now out of date
        files = [{"path": "/usr/bin/hello", "type": "file", "digest": "00" * 32}]
        f = open(os.path.join(tree, "header.json"), "w")
        json.dump({"tags": MINIMAL_TAGS, "files": files}, f)
        f.close()

        pkg = os.path.join(self.output_dir, "hello.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        os.chmod(program, 0o644)

        self.assertNotEqual(rc, 0)
        self.assertIn("unable to compute the digest of %s" % program, err)
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


# a digest that is the right length for sha256 but wrong for any file
BAD_DIGEST = "dead" + ("0" * 60)


class KeepDigestsBase(object):
    """Shared setup that builds a package with one regular file in it"""

    def setUp(self):
        super().setUp()

        content = rpmfluff.SourceFile(
            "README", make_random_content(256).encode("utf-8")
        )
        self.filename = "/usr/share/doc/%s/README" % NAME
        self.rpm.add_installed_file(self.filename, content)

    def drop_signature(self, tree):
        """Remove signature.json so -D computes the header digests rpm -qp checks"""
        os.unlink(os.path.join(tree, "signature.json"))

        return

    def extract_tree(self):
        """Build the package and extract it, returning the tree directory"""
        self.rpm.do_make()
        original_rpm = self.rpm.get_built_rpm(get_expected_arch())

        tree = os.path.join(self.output_dir, "extracted")
        os.makedirs(tree)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", original_rpm, "-O", tree])
        self.assertEqual(rc, 0, msg=err)

        return tree

    def read_header(self, tree):
        """Read the header.json out of an extracted tree"""
        f = open(os.path.join(tree, "header.json"))
        header = json.load(f)
        f.close()

        return header

    def write_header(self, tree, header):
        """Write a header.json back in to an extracted tree"""
        f = open(os.path.join(tree, "header.json"), "w")
        json.dump(header, f)
        f.close()

        return

    def file_digest(self, pkg):
        """Return the digest the package records for our file"""
        pairs = query_rpm(pkg, "[%{filenames} %{filedigests}\n]").splitlines()

        for pair in pairs:
            name, _, digest = pair.partition(" ")

            if name == self.filename:
                return digest

        return None


class CreateKeepsHeaderDigest(KeepDigestsBase, TestUnpackRPM):
    """With -D a hand edited digest reaches the package unchecked"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)

        # replace the digest of our file with a known bad value
        for entry in header["files"]:
            if entry.get("path") == self.filename:
                entry["digest"] = BAD_DIGEST

        self.write_header(tree, header)
        self.drop_signature(tree)

        pkg = os.path.join(self.output_dir, "bad.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # the bad digest is in the package as we wrote it
        self.assertEqual(self.file_digest(pkg), BAD_DIGEST)


class CreateRecomputesDigestWithoutFlag(KeepDigestsBase, TestUnpackRPM):
    """Without -D a hand edited digest is recomputed from the payload"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)

        good = None

        for entry in header["files"]:
            if entry.get("path") == self.filename:
                good = entry["digest"]
                entry["digest"] = BAD_DIGEST

        self.write_header(tree, header)

        pkg = os.path.join(self.output_dir, "good.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # the payload digest replaced the bad one we wrote
        self.assertEqual(self.file_digest(pkg), good)
        self.assertNotEqual(self.file_digest(pkg), BAD_DIGEST)


class CreateKeepDigestsStillComputesMissing(KeepDigestsBase, TestUnpackRPM):
    """With -D a file missing its digest still gets one computed"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)

        good = None

        for entry in header["files"]:
            if entry.get("path") == self.filename:
                good = entry["digest"]
                del entry["digest"]

        self.write_header(tree, header)
        self.drop_signature(tree)

        pkg = os.path.join(self.output_dir, "filled.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # the missing digest was computed, matching the payload
        self.assertEqual(self.file_digest(pkg), good)


def package_parts(pkg):
    """Return the main header and the payload of an RPM as bytes"""
    f = open(pkg, "rb")
    f.seek(96)

    # skip the signature header and the padding after it
    magic, reserved, nentries, nbytes = struct.unpack(">IIII", f.read(16))
    f.seek((16 * nentries) + nbytes, 1)
    f.seek((8 - (f.tell() % 8)) % 8, 1)

    magic, reserved, nentries, nbytes = struct.unpack(">IIII", f.read(16))
    f.seek(-16, 1)
    header = f.read(16 + (16 * nentries) + nbytes)
    payload = f.read()
    f.close()

    return (header, payload)


def header_sha256(pkg):
    """Return the SHA-256 digest of the main header of an RPM"""
    header, payload = package_parts(pkg)

    return hashlib.sha256(header).hexdigest()


def payload_sha256(pkg):
    """Return the SHA-256 digest of the payload of an RPM"""
    header, payload = package_parts(pkg)

    return hashlib.sha256(payload).hexdigest()


class KeepAllDigestsBase(KeepDigestsBase):
    """Shared helpers for the signature.json and payload digest tests"""

    def read_signature(self, tree):
        """Read the signature.json out of an extracted tree"""
        f = open(os.path.join(tree, "signature.json"))
        signature = json.load(f)
        f.close()

        return signature

    def write_signature(self, path, signature):
        """Write a signature.json to path"""
        f = open(path, "w")
        json.dump(signature, f)
        f.close()

        return

    def tag_value(self, data, name):
        """Return the value of a tag in header or signature JSON"""
        for entry in data["tags"]:
            if entry.get("tag") == name:
                value = entry.get("value")

                if isinstance(value, list):
                    return value[0]

                return value

        return None

    def set_tag_value(self, data, name, value):
        """Set the value of a tag in header or signature JSON"""
        for entry in data["tags"]:
            if entry.get("tag") == name:
                if isinstance(entry.get("value"), list):
                    entry["value"] = [value]
                else:
                    entry["value"] = value

        return

    def repacked(self, pkg):
        """Extract a package we created and return the tree directory"""
        tree = os.path.join(self.output_dir, "repacked")
        os.makedirs(tree)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", tree])
        self.assertEqual(rc, 0, msg=err)

        return tree


class CreateKeepsSignatureDigest(KeepAllDigestsBase, TestUnpackRPM):
    """With -D a hand edited signature digest reaches the package"""

    def runTest(self):
        tree = self.extract_tree()
        signature = self.read_signature(tree)
        self.set_tag_value(signature, "Sha256", BAD_DIGEST)
        self.write_signature(os.path.join(tree, "signature.json"), signature)

        pkg = os.path.join(self.output_dir, "bad.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        signature = self.read_signature(self.repacked(pkg))
        self.assertEqual(self.tag_value(signature, "Sha256"), BAD_DIGEST)


class CreateKeepDigestsComputesWrongLength(KeepAllDigestsBase, TestUnpackRPM):
    """With -D a signature digest of the wrong length is computed"""

    def runTest(self):
        tree = self.extract_tree()
        bad = {"Md5": "AAAA", "Sha1": "abc", "Sha256": "abc", "Sha3_256": "abc"}
        names = {"4": ["Md5", "Sha1", "Sha256"], "6": ["Sha256", "Sha3_256"]}

        for rpmformat in ["4", "6"]:
            if rpmformat == "6" and not HAVE_RPM_FORMAT_SIX:
                continue

            # a tree of this format, so -D keeps digests that fit it
            plain = os.path.join(self.output_dir, "plain%s.rpm" % rpmformat)
            args = [self.tarpm, "-c", "-F", rpmformat, "-f", plain, tree]
            rc, out, err = run_tarpm(args)
            self.assertEqual(rc, 0, msg=err)

            again = os.path.join(self.output_dir, "tree%s" % rpmformat)
            rc, out, err = run_tarpm([self.tarpm, "-x", "-f", plain, "-O", again])
            self.assertEqual(rc, 0, msg=err)

            signature = self.read_signature(again)

            for name, value in bad.items():
                self.set_tag_value(signature, name, value)

            self.write_signature(os.path.join(again, "signature.json"), signature)

            pkg = os.path.join(self.output_dir, "kept%s.rpm" % rpmformat)
            args = [self.tarpm, "-D", "-c", "-F", rpmformat, "-f", pkg, again]
            rc, out, err = run_tarpm(args)
            self.assertEqual(rc, 0, msg=err)

            for name in names[rpmformat]:
                self.assertIn("%s in signature.json has the wrong length" % name, err)

            # every digest was computed, so we get the same package
            f = open(pkg, "rb")
            kept = f.read()
            f.close()

            f = open(plain, "rb")
            self.assertEqual(kept, f.read())
            f.close()


class CreateRecomputesSignatureDigestWithoutFlag(KeepAllDigestsBase, TestUnpackRPM):
    """Without -D signature.json is not read and the digest is computed"""

    def runTest(self):
        tree = self.extract_tree()
        signature = self.read_signature(tree)
        self.set_tag_value(signature, "Sha256", BAD_DIGEST)
        self.write_signature(os.path.join(tree, "signature.json"), signature)

        pkg = os.path.join(self.output_dir, "good.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        signature = self.read_signature(self.repacked(pkg))
        self.assertEqual(self.tag_value(signature, "Sha256"), header_sha256(pkg))


class CreateKeepsSignatureDigestFromPath(KeepAllDigestsBase, TestUnpackRPM):
    """With -D and -S the signature digests come from the named file"""

    def runTest(self):
        tree = self.extract_tree()
        signature = self.read_signature(tree)
        os.unlink(os.path.join(tree, "signature.json"))

        self.set_tag_value(signature, "Sha256", BAD_DIGEST)
        sigpath = os.path.join(self.output_dir, "elsewhere.json")
        self.write_signature(sigpath, signature)

        pkg = os.path.join(self.output_dir, "bad.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-D", "-S", sigpath, "-c", "-f", pkg, tree]
        )
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(err.find("-S is ignored"), -1)

        signature = self.read_signature(self.repacked(pkg))
        self.assertEqual(self.tag_value(signature, "Sha256"), BAD_DIGEST)


class CreateKeepDigestsMissingSignatureFile(KeepAllDigestsBase, TestUnpackRPM):
    """With -D and -S naming a missing file tarpm fails"""

    def runTest(self):
        tree = self.extract_tree()
        sigpath = os.path.join(self.output_dir, "missing.json")

        pkg = os.path.join(self.output_dir, "bad.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-D", "-S", sigpath, "-c", "-f", pkg, tree]
        )
        self.assertNotEqual(rc, 0)
        self.assertFalse(os.path.exists(pkg))


class CreateKeepDigestsComputesSignature(KeepAllDigestsBase, TestUnpackRPM):
    """With -D and no signature.json the signature digests are computed"""

    def runTest(self):
        tree = self.extract_tree()
        os.unlink(os.path.join(tree, "signature.json"))

        pkg = os.path.join(self.output_dir, "computed.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        signature = self.read_signature(self.repacked(pkg))
        self.assertEqual(self.tag_value(signature, "Sha256"), header_sha256(pkg))


class CreateKeepsPayloadDigest(KeepAllDigestsBase, TestUnpackRPM):
    """With -D a hand edited payload digest in header.json reaches the package"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)
        self.set_tag_value(header, PAYLOAD_DIGEST, BAD_DIGEST)
        self.write_header(tree, header)

        pkg = os.path.join(self.output_dir, "bad.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        header = self.read_header(self.repacked(pkg))
        self.assertEqual(self.tag_value(header, PAYLOAD_DIGEST), BAD_DIGEST)


class CreateRecomputesPayloadDigestWithoutFlag(KeepAllDigestsBase, TestUnpackRPM):
    """Without -D a hand edited payload digest is recomputed"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)
        self.set_tag_value(header, PAYLOAD_DIGEST, BAD_DIGEST)
        self.write_header(tree, header)

        pkg = os.path.join(self.output_dir, "good.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        header = self.read_header(self.repacked(pkg))
        self.assertEqual(self.tag_value(header, PAYLOAD_DIGEST), payload_sha256(pkg))

        rc, err = verify_rpm(pkg)
        self.assertEqual(rc, 0, msg=err)


class CreateKeepDigestsComputesMissingPayloadDigest(KeepAllDigestsBase, TestUnpackRPM):
    """With -D a payload digest missing from header.json is computed"""

    def runTest(self):
        tree = self.extract_tree()
        header = self.read_header(tree)
        header["tags"] = [t for t in header["tags"] if t.get("tag") != PAYLOAD_DIGEST]
        self.write_header(tree, header)

        pkg = os.path.join(self.output_dir, "filled.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-D", "-c", "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # the missing digest was computed from the payload we wrote
        header = self.read_header(self.repacked(pkg))
        self.assertEqual(self.tag_value(header, PAYLOAD_DIGEST), payload_sha256(pkg))
