#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import stat
import rpmfluff
from baseclass import NAME, VER, REL
from baseclass import TestUnpackSRPM, TestUnpackRPM


def make_random_content(size):
    """Generate random printable ASCII content of given size"""
    import random
    import string

    return "".join(random.choices(string.ascii_letters + string.digits + " \n", k=size))


class VerifyPayloadExtractSimpleRPM(TestUnpackRPM):
    """Test extraction of a simple RPM with basic files"""

    def setUp(self):
        super().setUp()

        # Add a simple text file to the package
        readme_content = make_random_content(1024)
        readme = rpmfluff.SourceFile("README", readme_content.encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

        binary_content = make_random_content(2048)
        binary = rpmfluff.SourceFile("%s-bin" % NAME, binary_content.encode("utf-8"))
        self.rpm.add_installed_file("/usr/bin/%s" % NAME, binary, mode="755")

    def runTest(self):
        super().runTest()

        # Verify the payload directory was created
        payload_dir = os.path.join(self.output_dir, "payload")
        self.assertTrue(os.path.isdir(payload_dir))

        # Verify the files were extracted
        readme = os.path.join(payload_dir, "usr/share/doc", NAME, "README")
        binary = os.path.join(payload_dir, "usr/bin", NAME)

        self.assertTrue(os.path.isfile(readme))
        self.assertTrue(os.path.isfile(binary))

        # Verify file sizes
        self.assertEqual(os.path.getsize(readme), 1024)
        self.assertEqual(os.path.getsize(binary), 2048)

        # Verify the binary is executable
        mode = os.stat(binary).st_mode
        self.assertTrue(mode & stat.S_IXUSR)


class VerifyPayloadExtractDirectories(TestUnpackRPM):
    """Test extraction of RPM with directory structure"""

    def setUp(self):
        super().setUp()

        # Add files in nested directories
        config = rpmfluff.SourceFile(
            "config.conf", make_random_content(512).encode("utf-8")
        )
        self.rpm.add_installed_file("/etc/%s/config.conf" % NAME, config)

        data = rpmfluff.SourceFile("data.txt", make_random_content(256).encode("utf-8"))
        self.rpm.add_installed_file("/var/lib/%s/data.txt" % NAME, data)

        subfile = rpmfluff.SourceFile(
            "file.txt", make_random_content(128).encode("utf-8")
        )
        self.rpm.add_installed_file("/var/lib/%s/subdir/file.txt" % NAME, subfile)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        # Verify all directories were created
        self.assertTrue(os.path.isdir(os.path.join(payload_dir, "etc", NAME)))
        self.assertTrue(os.path.isdir(os.path.join(payload_dir, "var/lib", NAME)))
        self.assertTrue(
            os.path.isdir(os.path.join(payload_dir, "var/lib", NAME, "subdir"))
        )

        # Verify files exist
        config = os.path.join(payload_dir, "etc", NAME, "config.conf")
        data = os.path.join(payload_dir, "var/lib", NAME, "data.txt")
        subfile = os.path.join(payload_dir, "var/lib", NAME, "subdir/file.txt")

        self.assertTrue(os.path.isfile(config))
        self.assertTrue(os.path.isfile(data))
        self.assertTrue(os.path.isfile(subfile))

        # Verify file sizes
        self.assertEqual(os.path.getsize(config), 512)
        self.assertEqual(os.path.getsize(data), 256)
        self.assertEqual(os.path.getsize(subfile), 128)


class VerifyPayloadExtractSymlink(TestUnpackRPM):
    """Test extraction of RPM with symbolic links"""

    def setUp(self):
        super().setUp()

        # Add a regular file
        original = rpmfluff.SourceFile(
            "original.txt", make_random_content(1024).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/%s/original.txt" % NAME, original)

        # Add a symbolic link
        self.rpm.add_installed_symlink("/usr/share/%s/link.txt" % NAME, "original.txt")

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        original = os.path.join(payload_dir, "usr/share", NAME, "original.txt")
        link = os.path.join(payload_dir, "usr/share", NAME, "link.txt")

        # Verify the original file exists
        self.assertTrue(os.path.isfile(original))
        self.assertEqual(os.path.getsize(original), 1024)

        # Verify the symlink exists and points to the right target
        self.assertTrue(os.path.islink(link))
        self.assertEqual(os.readlink(link), "original.txt")

        # Verify reading through the symlink works
        self.assertTrue(os.path.isfile(link))
        self.assertEqual(os.path.getsize(link), 1024)


class VerifyPayloadExtractMultipleFiles(TestUnpackRPM):
    """Test extraction of RPM with multiple files"""

    def setUp(self):
        super().setUp()

        # Add multiple files with various sizes
        for i in range(10):
            size = 100 * (i + 1)
            content = rpmfluff.SourceFile(
                "file%d.txt" % i, make_random_content(size).encode("utf-8")
            )
            self.rpm.add_installed_file("/usr/share/%s/file%d.txt" % (NAME, i), content)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        # Verify all files were extracted with correct sizes
        for i in range(10):
            filepath = os.path.join(payload_dir, "usr/share", NAME, "file%d.txt" % i)
            self.assertTrue(os.path.isfile(filepath))
            self.assertEqual(os.path.getsize(filepath), 100 * (i + 1))


class VerifyPayloadExtractFilePermissions(TestUnpackRPM):
    """Test extraction preserves file permissions"""

    def setUp(self):
        super().setUp()

        # Add files with different permissions
        executable = rpmfluff.SourceFile(
            "executable", make_random_content(512).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/usr/bin/%s-executable" % NAME, executable, mode="755"
        )

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
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        executable = os.path.join(payload_dir, "usr/bin", "%s-executable" % NAME)
        readonly = os.path.join(payload_dir, "etc", NAME, "readonly.conf")
        normal = os.path.join(payload_dir, "usr/share", NAME, "normal.txt")

        # Verify files exist
        self.assertTrue(os.path.isfile(executable))
        self.assertTrue(os.path.isfile(readonly))
        self.assertTrue(os.path.isfile(normal))

        # Verify permissions
        exec_mode = os.stat(executable).st_mode & 0o777
        readonly_mode = os.stat(readonly).st_mode & 0o777
        normal_mode = os.stat(normal).st_mode & 0o777

        self.assertEqual(exec_mode, 0o755)
        self.assertEqual(readonly_mode, 0o444)
        self.assertEqual(normal_mode, 0o644)


class VerifyPayloadExtractEmptyFile(TestUnpackRPM):
    """Test extraction of RPM with empty files"""

    def setUp(self):
        super().setUp()

        # Add an empty file
        empty = rpmfluff.SourceFile("empty.txt", b"")
        self.rpm.add_installed_file("/usr/share/%s/empty.txt" % NAME, empty)

        nonempty = rpmfluff.SourceFile(
            "nonempty.txt", make_random_content(100).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/%s/nonempty.txt" % NAME, nonempty)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        empty = os.path.join(payload_dir, "usr/share", NAME, "empty.txt")
        nonempty = os.path.join(payload_dir, "usr/share", NAME, "nonempty.txt")

        # Verify both files exist
        self.assertTrue(os.path.isfile(empty))
        self.assertTrue(os.path.isfile(nonempty))

        # Verify sizes
        self.assertEqual(os.path.getsize(empty), 0)
        self.assertEqual(os.path.getsize(nonempty), 100)


class VerifyPayloadExtractSRPM(TestUnpackSRPM):
    """Test extraction of a source RPM payload"""

    def runTest(self):
        super().runTest()

        # Verify the payload directory was created
        payload_dir = os.path.join(self.output_dir, "payload")
        self.assertTrue(os.path.isdir(payload_dir))

        # SRPM should contain at least the spec file
        spec_file = os.path.join(payload_dir, "%s.spec" % NAME)
        self.assertTrue(os.path.isfile(spec_file))

        # Verify the spec file has content
        self.assertGreater(os.path.getsize(spec_file), 0)

        # Read and verify spec file contains expected content
        with open(spec_file, "r") as f:
            spec_content = f.read()
            self.assertIn("Name: %s" % NAME, spec_content)
            self.assertIn("Version: %s" % VER, spec_content)
            self.assertIn("Release: %s" % REL, spec_content)


class VerifyPayloadExtractWithSources(TestUnpackSRPM):
    """Test extraction of SRPM with source files"""

    def setUp(self):
        super().setUp()

        # Add a source tarball
        source = rpmfluff.SourceFile("source.tar.gz", b"fake tarball content")
        self.rpm.add_source(source)

        # Add a patch (applyPatch=False since we're just testing extraction)
        patch = rpmfluff.SourceFile("fix.patch", b"fake patch content")
        self.rpm.add_patch(patch, applyPatch=False)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        # Verify source and patch files were extracted
        source_file = os.path.join(payload_dir, "source.tar.gz")
        patch_file = os.path.join(payload_dir, "fix.patch")

        self.assertTrue(os.path.isfile(source_file))
        self.assertTrue(os.path.isfile(patch_file))

        # Verify content
        with open(source_file, "rb") as f:
            self.assertEqual(f.read(), b"fake tarball content")

        with open(patch_file, "rb") as f:
            self.assertEqual(f.read(), b"fake patch content")


class VerifyPayloadExtractDeepPath(TestUnpackRPM):
    """Test extraction with deeply nested paths"""

    def setUp(self):
        super().setUp()

        # Add a file with a very deep path
        deep_path = "/usr/share/%s/level1/level2/level3/level4/level5/deep.txt" % NAME
        deep_file = rpmfluff.SourceFile(
            "deep.txt", make_random_content(512).encode("utf-8")
        )
        self.rpm.add_installed_file(deep_path, deep_file)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        # Verify the deep file exists
        deep_file = os.path.join(
            payload_dir,
            "usr/share",
            NAME,
            "level1/level2/level3/level4/level5/deep.txt",
        )

        self.assertTrue(os.path.isfile(deep_file))
        self.assertEqual(os.path.getsize(deep_file), 512)


class VerifyPayloadExtractSpecialChars(TestUnpackRPM):
    """Test extraction with filenames containing special characters"""

    def setUp(self):
        super().setUp()

        # Add files with special characters (that are valid in filesystems)
        dash_file = rpmfluff.SourceFile(
            "file-with-dash.txt", make_random_content(100).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/file-with-dash.txt" % NAME, dash_file
        )

        underscore_file = rpmfluff.SourceFile(
            "file_with_underscore.txt", make_random_content(100).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/file_with_underscore.txt" % NAME, underscore_file
        )

        dots_file = rpmfluff.SourceFile(
            "file.with.dots.txt", make_random_content(100).encode("utf-8")
        )
        self.rpm.add_installed_file(
            "/usr/share/%s/file.with.dots.txt" % NAME, dots_file
        )

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")

        # Verify all files were extracted
        files = ["file-with-dash.txt", "file_with_underscore.txt", "file.with.dots.txt"]

        for filename in files:
            filepath = os.path.join(payload_dir, "usr/share", NAME, filename)
            self.assertTrue(os.path.isfile(filepath))
            self.assertEqual(os.path.getsize(filepath), 100)
