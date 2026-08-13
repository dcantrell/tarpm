#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import subprocess
import rpmfluff
from baseclass import NAME
from baseclass import TestUnpackRPM


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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
        (out, err) = proc.communicate()
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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
        original_rpm = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())

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
        (out, err) = proc.communicate()
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
