#
# Copyright The rpminspect Project Authors
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import shutil
import subprocess
import tempfile
import unittest
import rpmfluff

# NVRs to use for the fake test package
NAME = "vaporware"
VER = "0.1"
REL = "1"

VENDOR = "tarpm Test Vendor Ltd."

# Run the test suite with KEEP=y (or set to anything) in the
# environment to instruct the test suite to keep intermediate files
# and results.
if os.getenv("KEEP") is None:
    KEEP_RESULTS = False
else:
    KEEP_RESULTS = True


# Exceptions used by the test suite
class MissingTarpm(Exception):
    pass


# This is a local extension to SimpleRpmBuild in rpmfluff.  For the
# SRPM only test classes, override the do_make() function so that
# rpmbuild only builds the SRPM and not all of the packages.
#
# The only significant difference here is that rpmbuild is called
# with the '-bs' option instead of '-ba'.  Enough functions are
# duplicated here in order to make it all work.
class SimpleSrpmBuild(rpmfluff.SimpleRpmBuild):
    def __write_log(self, log, arch):
        log_dir = self.get_build_log_dir(arch)
        if not os.path.exists(log_dir):
            os.makedirs(log_dir)
        filename = self.get_build_log_path(arch)
        f = open(filename, "wb")
        for line in log:
            f.write(line)
        f.close()

    def __create_directories(self):
        """Sets up the directory hierarchy for the build"""
        if hasattr(self, "tmpdir"):
            if self.tmpdir and not (
                self.tmpdir_location and os.path.isdir(self.tmpdir_location)
            ):
                self.tmpdir_location = tempfile.mkdtemp(prefix="rpmfluff-")
        os.mkdir(self.get_base_dir())

        # Make fake rpmbuild directories
        for subDir in ["BUILD", "SOURCES", "SRPMS", "RPMS"]:
            os.mkdir(os.path.join(self.get_base_dir(), subDir))

    def do_make(self):
        """
        Hook to actually perform the rpmbuild, gathering the necessary source files first
        """
        self.clean()

        self.__create_directories()

        specFileName = self.gather_spec_file(self.get_base_dir())

        sourcesDir = self.get_sources_dir()
        self.gather_sources(sourcesDir)

        absBaseDir = os.path.abspath(self.get_base_dir())

        buildArchs = ()
        if self.buildArchs:
            buildArchs = self.buildArchs
        else:
            if hasattr(rpmfluff, "utils"):
                buildArchs = (rpmfluff.utils.expectedArch,)
            else:
                buildArchs = (rpmfluff.expectedArch,)
        for arch in buildArchs:
            command = [
                "rpmbuild",
                "--nodeps",
                "--define",
                "_topdir %s" % absBaseDir,
                "--define",
                "_rpmfilename %%{ARCH}/%%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm",
                "-bs",
                "--target",
                arch,
                specFileName,
            ]
            try:
                log = subprocess.check_output(
                    command, stderr=subprocess.STDOUT
                ).splitlines(True)
            except subprocess.CalledProcessError as e:
                raise RuntimeError(
                    "rpmbuild command failed with exit status %s: %s\n%s"
                    % (e.returncode, e.cmd, e.output)
                )
            self.__write_log(log, arch)


# Base test case class that ensures we have 'tarpm' as an executable
# command.
class RequiresTarpm(unittest.TestCase):
    def setUp(self):
        super().setUp()

        # make sure we have the program
        self.tarpm = os.getenv("TARPM")

        if (
            self.tarpm is None
            or not os.path.isfile(self.tarpm)
            or not os.access(self.tarpm, os.X_OK)
        ):
            raise MissingTarpm

        # create a temporary directory for outputs
        self.output_dir = tempfile.mkdtemp(prefix="tarpm-integration-test")

        # output from a tarpm extraction
        self.lead = os.path.join(self.output_dir, "lead.json")
        self.signature = os.path.join(self.output_dir, "signature.json")
        self.header = os.path.join(self.output_dir, "header.json")

        # tarpm opts
        self.opts = []

        # expected exit code
        self.exitcode = 0

    def tearDown(self):
        if KEEP_RESULTS:
            print("\n>>> Output directory: %s" % self.output_dir)
        else:
            shutil.rmtree(self.output_dir, True)


# Base test case class that tests operations on a source RPM
class TestUnpackSRPM(RequiresTarpm):
    def setUp(self):
        super().setUp()
        self.rpm = SimpleSrpmBuild(NAME, VER, REL)

        # turn off all rpmbuild post processing stuff for the purposes of testing
        self.rpm.header += "\n%global __os_install_post %{nil}\n"

        # select extract mode by default
        self.mode = "-x"

    def runTest(self):
        self.rpm.do_make()

        args = (
            [self.tarpm, self.mode]
            + self.opts
            + ["-f", self.rpm.get_built_srpm(), "-O", self.output_dir]
        )
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (self.out, self.err) = proc.communicate()

        self.assertEqual(proc.returncode, self.exitcode)

    def tearDown(self):
        super().tearDown()

        if not KEEP_RESULTS:
            self.rpm.clean()


# Base test case class that tests operations on a binary RPM
class TestUnpackRPM(RequiresTarpm):
    def setUp(self):
        super().setUp()
        self.rpm = rpmfluff.SimpleRpmBuild(NAME, VER, REL)

        # turn off all rpmbuild post processing stuff for the purposes of testing
        self.rpm.header += "\n%global __os_install_post %{nil}\n"

        # select extract mode by default
        self.mode = "-x"

    def runTest(self):
        self.rpm.do_make()

        args = (
            [self.tarpm, self.mode]
            + self.opts
            + [
                "-f",
                self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch()),
                "-O",
                self.output_dir,
            ]
        )
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (self.out, self.err) = proc.communicate()

        self.assertEqual(proc.returncode, self.exitcode)

    def tearDown(self):
        super().tearDown()

        if not KEEP_RESULTS:
            self.rpm.clean()
