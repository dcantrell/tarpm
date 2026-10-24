#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import os
import shutil
import stat
import subprocess
import unittest
import tempfile
import rpmfluff
from baseclass import NAME, VER, REL
from baseclass import HAVE_RPM_FORMAT_SIX
from baseclass import TestUnpackSRPM, TestUnpackRPM

try:
    from rpmfluff.utils import get_expected_arch
except ImportError:
    from rpmfluff import get_expected_arch


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


class VerifyNoPayloadSkipsThePayload(TestUnpackRPM):
    """Test -n writes the metadata and leaves the payload alone"""

    def setUp(self):
        super().setUp()
        self.opts = ["-n"]

        readme = rpmfluff.SourceFile(
            "README", make_random_content(1024).encode("utf-8")
        )
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # the JSON metadata is all there
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "lead.json")))
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "signature.json")))
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "header.json")))

        # but no payload tree was made
        self.assertFalse(os.path.isdir(os.path.join(self.output_dir, "payload")))

        # the file list comes out of the header, so it is still whole
        f = open(os.path.join(self.output_dir, "header.json"))
        header = json.load(f)
        f.close()

        paths = [entry["path"] for entry in header["files"]]
        self.assertIn("/usr/share/doc/%s/README" % NAME, paths)


class VerifyNoPayloadWritesTagFiles(TestUnpackRPM):
    """Test -n still writes the tag values that go to their own file"""

    def setUp(self):
        super().setUp()
        self.opts = ["-n"]
        self.rpm.add_post("echo post")

    def runTest(self):
        super().runTest()

        self.assertTrue(
            os.path.isfile(os.path.join(self.output_dir, "description.txt"))
        )
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "postin.txt")))
        self.assertFalse(os.path.isdir(os.path.join(self.output_dir, "payload")))


class VerifyNoPayloadHeaderToStdout(TestUnpackRPM):
    """Test -n with the header on stdout, which is the scripting case"""

    def setUp(self):
        super().setUp()
        self.opts = ["-n", "-H", "-"]

    def runTest(self):
        super().runTest()

        header = json.loads(self.out.decode("utf-8"))

        self.assertIn("tags", header)
        self.assertFalse(os.path.isdir(os.path.join(self.output_dir, "payload")))

        tags = {tag["tag"]: tag.get("value") for tag in header["tags"]}
        self.assertEqual(tags["Name"], NAME)
        self.assertEqual(tags["Version"], VER)


class VerifyNoPayloadSRPM(TestUnpackSRPM):
    """Test -n on a source RPM"""

    def setUp(self):
        super().setUp()
        self.opts = ["-n"]

    def runTest(self):
        super().runTest()

        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "header.json")))

        # Check if SPEC tag exists in the SRPM. If it does, spec.txt should be extracted
        spec_output = (
            subprocess.Popen(
                ["rpm", "-qp", "--qf", "%{SPEC}", self.rpm.get_built_srpm()],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            .communicate()[0]
            .decode()
        )

        if spec_output and spec_output != "(none)":
            self.assertTrue(
                os.path.isfile(os.path.join(self.output_dir, "spec.txt")),
                msg="SPEC tag exists in SRPM but spec.txt was not extracted",
            )

        self.assertFalse(os.path.isdir(os.path.join(self.output_dir, "payload")))


class VerifyNoPayloadMetadataMatchesFullExtract(TestUnpackRPM):
    """Test -n writes the same metadata a full extraction writes"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", make_random_content(512).encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # the run above was a full extraction, so keep what it wrote
        f = open(os.path.join(self.output_dir, "header.json"))
        full = f.read()
        f.close()

        metadata_dir = os.path.join(self.output_dir, "metaonly")
        args = [
            self.tarpm,
            "-x",
            "-n",
            "-f",
            self.rpm.get_built_rpm(get_expected_arch()),
            "-O",
            metadata_dir,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err)

        f = open(os.path.join(metadata_dir, "header.json"))
        metaonly = f.read()
        f.close()

        self.assertEqual(full, metaonly)


class VerifyNoPayloadIgnoresPayloadDir(TestUnpackRPM):
    """Test -P says it is ignored when -n turns the payload off"""

    def setUp(self):
        super().setUp()
        self.payload_dir = os.path.join(self.output_dir, "elsewhere")
        self.opts = ["-n", "-P", self.payload_dir]

    def runTest(self):
        super().runTest()

        self.assertIn("-P is ignored when -n is specified", self.err.decode("utf-8"))
        self.assertFalse(os.path.isdir(self.payload_dir))


class VerifyOutputDirNotMadeWhenEverythingIsRedirected(TestUnpackRPM):
    """Test the output directory is left alone when nothing goes in it"""

    def setUp(self):
        super().setUp()

        # every piece lands somewhere other than the output directory
        self.elsewhere = tempfile.mkdtemp(prefix="tarpm-elsewhere")
        self.payload_dir = os.path.join(self.elsewhere, "payload")
        self.opts = [
            "-L",
            os.path.join(self.elsewhere, "lead.json"),
            "-S",
            os.path.join(self.elsewhere, "signature.json"),
            "-H",
            os.path.join(self.elsewhere, "header.json"),
            "-P",
            self.payload_dir,
        ]

        # the baseclass hands -O a directory that is already there, so
        # name one that is not to see whether tarpm makes it
        shutil.rmtree(self.output_dir, True)

    def runTest(self):
        super().runTest()

        self.assertFalse(os.path.exists(self.output_dir))

        for name in ["lead.json", "signature.json", "header.json"]:
            self.assertTrue(os.path.isfile(os.path.join(self.elsewhere, name)))

        # the tag value files sit next to header.json
        self.assertTrue(os.path.isfile(os.path.join(self.elsewhere, "description.txt")))
        self.assertTrue(os.path.isdir(self.payload_dir))

    def tearDown(self):
        super().tearDown()
        shutil.rmtree(self.elsewhere, True)


class VerifyOutputDirMadeWhenSomethingNeedsIt(TestUnpackRPM):
    """Test the output directory still shows up when a file goes in it"""

    def setUp(self):
        super().setUp()

        # the header goes elsewhere, so lead.json and signature.json
        # are what keep the output directory alive
        self.elsewhere = tempfile.mkdtemp(prefix="tarpm-elsewhere")
        self.opts = ["-n", "-H", os.path.join(self.elsewhere, "header.json")]
        shutil.rmtree(self.output_dir, True)

    def runTest(self):
        super().runTest()

        self.assertTrue(os.path.isdir(self.output_dir))
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "lead.json")))
        self.assertTrue(os.path.isfile(os.path.join(self.output_dir, "signature.json")))

        # the tag value files follow header.json, not the output directory
        self.assertTrue(os.path.isfile(os.path.join(self.elsewhere, "description.txt")))
        self.assertFalse(
            os.path.isfile(os.path.join(self.output_dir, "description.txt"))
        )

    def tearDown(self):
        super().tearDown()
        shutil.rmtree(self.elsewhere, True)


class VerifyMetadataPathDirectoriesAreMade(TestUnpackRPM):
    """Test a -H path several directories deep gets those made for it"""

    def setUp(self):
        super().setUp()
        self.header = os.path.join(self.output_dir, "a", "b", "c", "header.json")
        self.opts = ["-n", "-H", self.header]

    def runTest(self):
        super().runTest()

        self.assertTrue(os.path.isfile(self.header))


class VerifyUnwritableOutputDirFails(TestUnpackRPM):
    """Test an output directory we cannot make is an error, not a warning"""

    def setUp(self):
        super().setUp()
        self.opts = ["-n", "-O", "/proc/tarpm-cannot-make-this"]
        self.exitcode = 1

    def runTest(self):
        # the baseclass passes -O itself, so run tarpm directly here
        self.rpm.do_make()

        args = [
            self.tarpm,
            "-x",
            "-n",
            "-f",
            self.rpm.get_built_rpm(get_expected_arch()),
            "-O",
            "/proc/tarpm-cannot-make-this",
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()

        self.assertEqual(proc.returncode, 1)
        self.assertIn("unable to mkdir", err.decode("utf-8"))


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyExtractStrippedPayload(TestUnpackRPM):
    """Test extraction of the stripped cpio payload an RPM v6 format package carries"""

    def setUp(self):
        super().setUp()

        # rpm writes the stripped payload ("07070X") rather than newc
        # whenever the header carries Longfilesizes, which every
        # v6 format package does.  libarchive does not know that magic,
        # so this only works through the librpm fallback.
        self.rpm.header += "\n%global _rpmformat 6\n"

        readme = rpmfluff.SourceFile("README", make_random_content(256).encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        payload_dir = os.path.join(self.output_dir, "payload")
        readme = os.path.join(payload_dir, "usr/share/doc", NAME, "README")

        self.assertTrue(os.path.isdir(payload_dir))
        self.assertTrue(os.path.isfile(readme))
        self.assertEqual(os.path.getsize(readme), 256)


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyStrippedPayloadRoundTrip(TestUnpackRPM):
    """Test a v6 format package with a stripped payload survives a round trip"""

    def setUp(self):
        super().setUp()
        self.rpm.header += "\n%global _rpmformat 6\n"

        readme = rpmfluff.SourceFile("README", make_random_content(128).encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        pkg = os.path.join(self.output_dir, "again.rpm")
        args = [self.tarpm, "-c", "-F", "6", "-f", pkg, self.output_dir]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err)

        # the file list came back whole, which it does not when the
        # payload failed to unpack and the tree came out empty
        proc = subprocess.Popen(
            ["rpm", "-qlp", pkg], stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        out, err = proc.communicate()
        self.assertIn("/usr/share/doc/%s/README" % NAME, out.decode())

        # and we wrote the stripped payload back out the way rpm does
        f = open(self.rpm.get_built_rpm(get_expected_arch()), "rb")
        one = f.read()
        f.close()

        f = open(pkg, "rb")
        two = f.read()
        f.close()

        self.assertEqual(one, two, "the package came out different")


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyExtractStrippedPayloadHardLinks(TestUnpackRPM):
    """Test hard links in a stripped payload come out as hard links"""

    def setUp(self):
        super().setUp()
        self.rpm.header += "\n%global _rpmformat 6\n"

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
        super().runTest()

        docdir = os.path.join(self.output_dir, "payload", "usr/share", NAME)
        inodes = set()

        # the payload holds the data once, in the last entry of the
        # group, and every link has to end up with it
        for name in ["one.txt", "two.txt", "three.txt"]:
            path = os.path.join(docdir, name)

            with open(path, "rb") as f:
                self.assertEqual(f.read(), b"the same content\n", msg=name)

            sb = os.lstat(path)
            self.assertEqual(sb.st_nlink, 3, msg=name)
            inodes.add(sb.st_ino)

        self.assertEqual(len(inodes), 1)


class VerifyFailedFallbackLeavesNoTarFile(TestUnpackRPM):
    """Test the temporary tar file goes away when the fallback reader fails"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", make_random_content(128).encode("utf-8"))
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

        # a payload directory nothing can be made in makes both readers
        # fail, which is the path that used to leave the tar behind.
        # procfs refuses new directories even for root, where a mode
        # of 0500 does not.
        self.payload_dir = "/proc"

    def runTest(self):
        self.rpm.do_make()

        # the tar lands next to the package, so work on a copy of our
        # own where we can see it
        original = self.rpm.get_built_rpm(get_expected_arch())
        pkg = os.path.join(self.output_dir, os.path.basename(original))
        shutil.copyfile(original, pkg)

        args = [
            self.tarpm,
            "-x",
            "-f",
            pkg,
            "-O",
            os.path.join(self.output_dir, "tree"),
            "-P",
            self.payload_dir,
        ]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()

        self.assertNotEqual(proc.returncode, 0, msg=out)
        self.assertFalse(os.path.exists(pkg + ".tar"), msg="temporary tar left behind")
