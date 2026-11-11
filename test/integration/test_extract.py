#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import gzip
import json
import os
import resource
import shutil
import signal
import stat
import struct
import subprocess
import unittest
import tempfile
import rpmfluff
from baseclass import NAME, VER, REL
from baseclass import HAVE_RPM_FORMAT_SIX
from baseclass import RequiresTarpm
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


class VerifyFailedEntryIsExtractedOnce(TestUnpackRPM):
    """One entry that fails does not make the whole payload go in twice"""

    def setUp(self):
        super().setUp()
        self.opts = ["-v"]
        self.exitcode = 1

        # libarchive reads a v4 format package straight from the RPM
        self.rpm.header += "\n%global _rpmformat 4\n"

        for name in ["a.txt", "b.txt", "c.txt"]:
            self.rpm.add_installed_file(
                "/usr/share/%s/%s" % (NAME, name),
                rpmfluff.SourceFile(name, b"%s\n" % name.encode("utf-8")),
            )

        # a non-empty directory where b.txt goes makes that entry fail
        self.blocker = os.path.join(
            self.output_dir, "payload", "usr", "share", NAME, "b.txt", "blocker"
        )
        os.makedirs(self.blocker)

    def runTest(self):
        super().runTest()

        out = self.out.decode("utf-8").splitlines()
        err = self.err.decode("utf-8")

        # each path comes out once and the failure is reported once
        for name in ["a.txt", "b.txt", "c.txt"]:
            self.assertEqual(out.count("./usr/share/%s/%s" % (NAME, name)), 1, msg=out)

        self.assertEqual(err.count("Could not unlink"), 1, msg=err)

        # the entries around the failed one still went in
        for name in ["a.txt", "c.txt"]:
            path = os.path.join(self.output_dir, "payload", "usr", "share", NAME, name)
            self.assertTrue(os.path.isfile(path), msg=path)


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


class VerifyOutputFilesDoNotFollowSymlinks(TestUnpackRPM):
    """Test the JSON and tag files are not written through a symlink"""

    def runTest(self):
        self.rpm.do_make()
        pkg = self.rpm.get_built_rpm(get_expected_arch())
        target = os.path.join(self.output_dir, "target")

        for name in ["header.json", "description.txt"]:
            f = open(target, "w")
            f.write("keep me\n")
            f.close()

            dest = os.path.join(self.output_dir, name + ".out")
            os.makedirs(dest)
            os.symlink(target, os.path.join(dest, name))

            args = [self.tarpm, "-x", "-n", "-f", pkg, "-O", dest]
            proc = subprocess.Popen(
                args, stdout=subprocess.PIPE, stderr=subprocess.PIPE
            )
            out, err = proc.communicate()

            self.assertNotEqual(proc.returncode, 0)
            self.assertIn(
                "%s is a symlink, not writing through it" % name,
                err.decode("utf-8"),
            )

            f = open(target)
            self.assertEqual(f.read(), "keep me\n")
            f.close()


class VerifyVerboseKeepsDotfiles(TestUnpackRPM):
    """Test -v prints a dotfile with its leading dot"""

    def setUp(self):
        super().setUp()
        # at the top of the payload so the path starts "./."
        self.rpm.add_installed_file(
            "/.hidden", rpmfluff.SourceFile(".hidden", b"hidden\n")
        )
        self.opts = ["-v"]

    def runTest(self):
        super().runTest()
        lines = self.out.decode("utf-8").splitlines()
        self.assertIn("./.hidden", lines)
        self.assertNotIn("./hidden", lines)


class VerifyNevraCannotLeaveWorkingDir(TestUnpackRPM):
    """Test a Name with "../" does not move the extraction out of cwd"""

    def runTest(self):
        super().runTest()

        # rpmbuild refuses this name, so put it in with tarpm
        with open(self.header) as f:
            header = json.load(f)

        for tag in header["tags"]:
            if tag["tag"] == "Name":
                tag["value"] = "../../escaped"

        with open(self.header, "w") as f:
            json.dump(header, f)

        evil = os.path.join(self.output_dir, "evil.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", evil, self.output_dir],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode("utf-8"))
        self.assertIn("holds a slash", err.decode("utf-8"))

        # extracting with no -O in work/a/b must not write in work
        work = os.path.join(self.output_dir, "work")
        inner = os.path.join(work, "a", "b")
        os.makedirs(inner)

        proc = subprocess.Popen(
            [self.tarpm, "-x", "-f", evil],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=inner,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("cannot be used as a directory name", err.decode("utf-8"))
        self.assertEqual(sorted(os.listdir(work)), ["a"])
        self.assertEqual(os.listdir(inner), [])

        # naming the output directory still works
        named = os.path.join(inner, "named")
        proc = subprocess.Popen(
            [self.tarpm, "-x", "-f", evil, "-O", named],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode("utf-8"))
        self.assertTrue(os.path.isfile(os.path.join(named, "header.json")))


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

        # the tar lands in $TMPDIR, so give it one of our own
        original = self.rpm.get_built_rpm(get_expected_arch())
        pkg = os.path.join(self.output_dir, os.path.basename(original))
        shutil.copyfile(original, pkg)
        tmpdir = os.path.join(self.output_dir, "tmp")
        os.makedirs(tmpdir)
        env = dict(os.environ, TMPDIR=tmpdir)

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
        proc = subprocess.Popen(
            args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
        )
        out, err = proc.communicate()

        self.assertNotEqual(proc.returncode, 0, msg=out)
        self.assertEqual(os.listdir(tmpdir), [], msg="temporary tar left behind")
        self.assertFalse(
            os.path.exists(pkg + ".tar"), msg="tar written next to package"
        )


class RawTagsPackage(TestUnpackRPM):
    """Build a package and rewrite tag numbers so its arrays do not agree"""

    # tag numbers to rewrite, overridden by each test
    retag = {}
    array = None
    kept = []
    message = None

    def check(self):
        self.rpm.add_simple_payload_file()
        self.rpm.add_changelog_entry("second", VER, REL)

        # more requires than changelog entries and files
        for i in range(8):
            self.rpm.add_requires("raw-tags-%d" % i)

        self.rpm.do_make()

        f = open(self.rpm.get_built_rpm(get_expected_arch()), "rb")
        data = bytearray(f.read())
        f.close()

        # the lead is 96 bytes, then the signature header padded to 8
        nentries, nbytes = struct.unpack_from(">II", data, 96 + 8)
        hlen = 16 * nentries + nbytes
        hdr = 96 + 16 + hlen + (8 - (hlen % 8)) % 8
        nentries, nbytes = struct.unpack_from(">II", data, hdr + 8)
        found = 0

        for i in range(nentries):
            entry = hdr + 16 + 16 * i
            tag = struct.unpack_from(">I", data, entry)[0]

            if tag in self.retag:
                struct.pack_into(">I", data, entry, self.retag[tag])
                found += 1

        self.assertEqual(found, len(self.retag))

        pkg = os.path.join(self.output_dir, "raw.rpm")
        f = open(pkg, "wb")
        f.write(data)
        f.close()

        tree = os.path.join(self.output_dir, "tree")
        proc = subprocess.Popen(
            [self.tarpm, "-x", "-f", pkg, "-O", tree],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn(
            "unable to build the %s array, keeping the raw tags" % self.array,
            err.decode(),
        )

        f = open(os.path.join(tree, "header.json"))
        header = json.load(f)
        f.close()

        self.assertNotIn(self.array, header)
        tags = [t["tag"] for t in header["tags"]]

        for tag in self.kept:
            self.assertIn(tag, tags)

        # create turns the tree down instead of losing the data
        new = os.path.join(self.output_dir, "new.rpm")
        proc = subprocess.Popen(
            [self.tarpm, "-c", "-f", new, tree],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = proc.communicate()
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn(self.message, err.decode())
        self.assertFalse(os.path.exists(new))


class ExtractKeepsMismatchedChangelog(RawTagsPackage):
    """Changelog arrays of differing lengths stay as raw tags"""

    # swap Changelogname and Requirename
    retag = {1081: 1049, 1049: 1081}
    array = "changelog"
    kept = ["Changelogtime", "Changelogname", "Changelogtext"]
    message = "changelog tags in header.json have mismatched lengths"

    def runTest(self):
        self.check()

        # the file list is fine, so the payload still comes out
        payload = os.path.join(self.output_dir, "tree", "payload", "usr")
        self.assertTrue(os.path.isdir(payload))


class ExtractKeepsMismatchedFileList(RawTagsPackage):
    """File list arrays of differing lengths stay as raw tags"""

    # swap Dirindexes and Requireflags
    retag = {1116: 1048, 1048: 1116}
    array = "files"
    kept = ["Basenames", "Dirnames", "Dirindexes", "Filemodes"]
    message = "from the tags in header.json"

    def runTest(self):
        self.check()


class ExtractKeepsPartialFileList(RawTagsPackage):
    """A file list with one of its three main tags gone stays as raw tags"""

    # turn Dirindexes in to a tag nothing reads
    retag = {1116: 1999}
    array = "files"
    kept = ["Basenames", "Dirnames", "Filemodes"]
    message = "from the tags in header.json"

    def runTest(self):
        self.check()


class VerifyMalformedHeaderIsRejected(TestUnpackRPM):
    """A header whose sizes or offsets point outside its data is turned away"""

    def corrupt(self, name, change):
        """Write a copy of the package with change applied and return its path"""
        pkg = os.path.join(self.output_dir, name + ".rpm")
        data = bytearray(self.original)
        change(data)

        f = open(pkg, "wb")
        f.write(data)
        f.close()

        return pkg

    def runTest(self):
        self.rpm.do_make()

        f = open(self.rpm.get_built_rpm(get_expected_arch()), "rb")
        self.original = f.read()
        f.close()

        # the lead is 96 bytes, then the signature header padded to 8
        sig = 96
        nentries, nbytes = struct.unpack_from(">II", self.original, sig + 8)
        hlen = 16 * nentries + nbytes
        hdr = sig + 16 + hlen + (8 - (hlen % 8)) % 8
        nentries, nbytes = struct.unpack_from(">II", self.original, hdr + 8)
        index = hdr + 16
        datastart = index + 16 * nentries

        # the first index entry of type 6, which is RPM_STRING_TYPE
        string_entry = None

        for i in range(nentries):
            if struct.unpack_from(">I", self.original, index + 16 * i + 4)[0] == 6:
                string_entry = index + 16 * i
                break

        self.assertIsNotNone(string_entry)

        def too_many_entries(data):
            struct.pack_into(">I", data, hdr + 8, 0x10000001)

        def negative_offset(data):
            struct.pack_into(">i", data, index + 8, -16)

        def unterminated_string(data):
            struct.pack_into(">I", data, string_entry + 8, nbytes - 1)
            data[datastart + nbytes - 1] = ord("x")

        def truncated_signature(data):
            del data[sig + 24 :]  # noqa: E203

        cases = [
            ("toomany", too_many_entries),
            ("negative", negative_offset),
            ("unterminated", unterminated_string),
            ("truncated", truncated_signature),
        ]

        for name, change in cases:
            pkg = self.corrupt(name, change)
            args = [
                self.tarpm,
                "-x",
                "-f",
                pkg,
                "-O",
                os.path.join(self.output_dir, name),
            ]
            proc = subprocess.Popen(
                args, stdout=subprocess.PIPE, stderr=subprocess.PIPE
            )
            out, err = proc.communicate()

            # a negative return code is a signal, which means a crash
            self.assertGreater(proc.returncode, 0, msg="%s: %s" % (name, err.decode()))


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class LibrpmPayload(RequiresTarpm):
    """
    Class for tests of the librpm payload reader.  tarpm falls back to
    it when libarchive cannot read a payload, which is always the case
    for the stripped cpio archive in a v6 format package.
    """

    def setUp(self):
        super().setUp()

        # gzip so we can take the payload apart without other tools
        tags = [
            {"tag": "Name", "type": "string", "value": "hello"},
            {"tag": "Version", "type": "string", "value": "1.0"},
            {"tag": "Release", "type": "string", "value": "1"},
            {"tag": "Summary", "type": "i18n string", "value": "A greeting"},
            {"tag": "License", "type": "string", "value": "MIT"},
            {"tag": "Payloadcompressor", "type": "string", "value": "gzip"},
        ]

        self.tree = os.path.join(self.output_dir, "tree")
        sharedir = os.path.join(self.tree, "payload", "usr", "share", "hello")
        os.makedirs(sharedir)

        f = open(os.path.join(self.tree, "header.json"), "w")
        json.dump({"tags": tags}, f)
        f.close()

        for i in range(4):
            f = open(os.path.join(sharedir, "file%d" % i), "w")
            f.write(make_random_content(1024 * (i + 1)))
            f.close()

        f = open(os.path.join(sharedir, "big"), "w")
        f.write(make_random_content(256 * 1024))
        f.close()

        # the tar file the librpm reader writes goes here
        self.tmpdir = os.path.join(self.output_dir, "tmp")
        os.makedirs(self.tmpdir)

    def build(self):
        """Create the v6 format package and return its path"""
        pkg = os.path.join(self.output_dir, "hello.rpm")
        args = [self.tarpm, "-c", "-F", "6", "-f", pkg, self.tree]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode())

        return pkg

    def assertNoTarLeft(self):
        """Assert the tar file the librpm reader writes was removed"""
        self.assertEqual(os.listdir(self.tmpdir), [])

        for dirpath, dirnames, filenames in os.walk(self.output_dir):
            self.assertEqual([f for f in filenames if f.endswith(".tar")], [])

    def extract(self, pkg, name, preexec_fn=None, extra=None):
        """Extract a package and return (returncode, stderr)"""
        args = [self.tarpm, "-x", "-f", pkg]

        if name is not None:
            args += ["-O", os.path.join(self.output_dir, name)]

        if extra is not None:
            args += extra

        proc = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=preexec_fn,
            env=dict(os.environ, TMPDIR=self.tmpdir),
            cwd=self.output_dir,
        )
        out, err = proc.communicate()

        return (proc.returncode, err.decode())


class VerifyLibrpmPayloadNeedsNoTmpdir(LibrpmPayload):
    """The librpm reader writes straight to the payload tree and nowhere else"""

    def runTest(self):
        pkg = self.build()

        # a TMPDIR that is not there would fail any temporary file
        self.tmpdir = os.path.join(self.output_dir, "missing")
        tree = os.path.join(self.output_dir, "out")

        proc = subprocess.Popen(
            [self.tarpm, "-x", "-v", "-f", pkg, "-O", tree],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=dict(os.environ, TMPDIR=self.tmpdir),
        )
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode())

        sharedir = os.path.join(tree, "payload", "usr", "share", "hello")

        for i in range(4):
            f = open(os.path.join(sharedir, "file%d" % i))
            self.assertEqual(len(f.read()), 1024 * (i + 1))
            f.close()

        # -v names each entry once, the way the libarchive reader does
        lines = out.decode().splitlines()
        self.assertEqual(lines.count("./usr/share/hello/big"), 1, msg=lines)
        self.assertEqual(lines.count("./usr/share/hello"), 1, msg=lines)
        self.assertNotIn("./usr/share/hello/", lines)
        self.assertFalse(os.path.exists(self.tmpdir))


class VerifyCorruptPayloadFails(LibrpmPayload):
    """A payload only librpm can read fails when one of its entries is bad"""

    def runTest(self):
        pkg = self.build()

        # the package is fine before we break it
        rc, err = self.extract(pkg, "good")
        self.assertEqual(rc, 0, msg=err)

        f = open(pkg, "rb")
        data = f.read()
        f.close()

        # the lead, the signature header padded to 8, then the main header
        offset = 96

        for pad in [True, False]:
            nindex, hsize = struct.unpack_from(">II", data, offset + 8)
            offset += 16 + (16 * nindex) + hsize

            if pad:
                offset += (8 - (offset % 8)) % 8

        # a v6 format payload is a stripped cpio archive with this magic
        cpio = bytearray(gzip.decompress(data[offset:]))
        first = cpio.find(b"07070X")
        second = cpio.find(b"07070X", first + 1)
        self.assertNotEqual(second, -1)
        cpio[second : second + 6] = b"BADMAG"  # noqa: E203

        bad = os.path.join(self.output_dir, "bad.rpm")
        f = open(bad, "wb")
        f.write(data[:offset] + gzip.compress(bytes(cpio)))
        f.close()

        rc, err = self.extract(bad, "bad")
        self.assertGreater(rc, 0, msg=err)
        self.assertIn("error reading RPM payload", err)

        self.assertNoTarLeft()


class VerifyPayloadTarIgnoresPlantedSymlink(LibrpmPayload):
    """The librpm reader does not write through a symlink next to the package"""

    def runTest(self):
        pkg = self.build()

        # the old tar file name, pointing at a file we want left alone
        victim = os.path.join(self.output_dir, "victim")
        f = open(victim, "w")
        f.write("keep me\n")
        f.close()
        os.symlink(victim, pkg + ".tar")

        rc, err = self.extract(pkg, "tree")
        self.assertEqual(rc, 0, msg=err)

        f = open(victim)
        self.assertEqual(f.read(), "keep me\n")
        f.close()

        readme = os.path.join(self.output_dir, "tree", "payload", "usr/share/hello/big")
        self.assertTrue(os.path.isfile(readme))

        os.unlink(pkg + ".tar")
        self.assertNoTarLeft()


class VerifyPayloadTarMakesNoOutputDir(LibrpmPayload):
    """The librpm reader makes no NEVRA directory when every output goes elsewhere"""

    def runTest(self):
        pkg = self.build()

        other = os.path.join(self.output_dir, "other")
        os.makedirs(other)
        extra = [
            "-L",
            os.path.join(other, "lead.json"),
            "-S",
            os.path.join(other, "signature.json"),
            "-H",
            os.path.join(other, "header.json"),
            "-P",
            os.path.join(other, "payload"),
        ]

        before = set(os.listdir(self.output_dir))
        rc, err = self.extract(pkg, None, extra=extra)
        self.assertEqual(rc, 0, msg=err)

        self.assertEqual(set(os.listdir(self.output_dir)), before)
        self.assertTrue(
            os.path.isfile(os.path.join(other, "payload", "usr/share/hello/big"))
        )
        self.assertNoTarLeft()


def limit_file_size():
    """Cap the size of files tarpm writes so a write fails partway"""
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024, 64 * 1024))


class VerifyPayloadWriteErrorFails(LibrpmPayload):
    """A failed write of the tar file the librpm reader makes is an error"""

    def runTest(self):
        pkg = self.build()

        # the 256 KiB file goes past the limit in the tar file
        rc, err = self.extract(pkg, "limited", preexec_fn=limit_file_size)
        self.assertGreater(rc, 0, msg=err)
        self.assertIn("archive_write_data", err)

        self.assertNoTarLeft()
