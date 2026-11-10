#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import subprocess

from baseclass import RequiresTarpm
from baseclass import TestUnpackRPM

try:
    from rpmfluff.utils import get_expected_arch
except ImportError:
    from rpmfluff import get_expected_arch


# Verify --help gives help output
class TarpmHelp(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0)

        out = out.splitlines()
        t1 = list(filter(lambda x: x.startswith(b"Usage:"), out))
        t2 = list(filter(lambda x: x.startswith(b"Options:"), out))

        self.assertEqual(len(t1), 1)
        self.assertEqual(len(t2), 1)


# Verify -t, -x, or -c is required
class TarpmRequiredOption(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, self.tarpm],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 1)
        s = "must specify at least"
        self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify tarpm doesn't segfault on non-RPM files
class TarpmSegv(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "-x", "-f", self.tarpm],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 1)
        s = "is not a valid RPM"
        self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the payload metadata options are rejected when extracting
class TarpmPayloadOverrideExtract(RequiresTarpm):
    def runTest(self):
        for opt in ["-m", "-u", "-g", "-l", "-a"]:
            p = subprocess.Popen(
                [self.tarpm, "-x", opt, "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "may only be used when creating an RPM"
            self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the payload metadata options are rejected when listing
class TarpmPayloadOverrideList(RequiresTarpm):
    def runTest(self):
        for opt in ["-m", "-u", "-g", "-l", "-a"]:
            p = subprocess.Popen(
                [self.tarpm, "-t", opt, "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "may only be used when creating an RPM"
            self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the payload metadata options show up in the help output
class TarpmPayloadOverrideHelp(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0)

        out = out.decode("utf-8")

        for opt in [
            "--payload-mtime",
            "--payload-user",
            "--payload-group",
            "--payload-linkto",
            "--payload-all",
        ]:
            self.assertNotEqual(out.find(opt), -1)


# Verify the keep digests option is rejected when extracting or listing
class TarpmKeepDigestsWrongMode(RequiresTarpm):
    def runTest(self):
        for mode in ["-x", "-t"]:
            p = subprocess.Popen(
                [self.tarpm, mode, "-D", "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "-D may only be used when creating an RPM"
            self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the keep digests option shows up in the help output
class TarpmKeepDigestsHelp(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0)

        self.assertNotEqual(out.decode("utf-8").find("--keep-digests"), -1)


# Verify the keep signatures option is rejected when extracting or listing
class TarpmKeepSignaturesWrongMode(RequiresTarpm):
    def runTest(self):
        for mode in ["-x", "-t"]:
            p = subprocess.Popen(
                [self.tarpm, mode, "-k", "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "-k may only be used when creating an RPM"
            self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the keep signatures option shows up in the help output
class TarpmKeepSignaturesHelp(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0)

        self.assertNotEqual(out.decode("utf-8").find("--keep-signatures"), -1)


# Verify the no payload option is rejected when listing or creating
class TarpmNoPayloadWrongMode(RequiresTarpm):
    def runTest(self):
        for mode in ["-t", "-c"]:
            p = subprocess.Popen(
                [self.tarpm, mode, "-n", "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "-n may only be used when extracting an RPM"
            self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify the no payload option shows up in the help output
class TarpmNoPayloadHelp(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "--help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0)

        self.assertNotEqual(out.decode("utf-8").find("--no-payload"), -1)


# Verify an unknown letter in the tar style options is an error
class TarpmUnknownShortLetter(RequiresTarpm):
    def runTest(self):
        p = subprocess.Popen(
            [self.tarpm, "tqf", self.tarpm],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 1)
        s = "unknown option letter 'q' in \"tqf\""
        self.assertNotEqual(err.decode("utf-8").find(s), -1)


# Verify an empty -O or -P name is an error rather than /
class TarpmEmptyOutputName(RequiresTarpm):
    def runTest(self):
        for opt in ["-O", "-P"]:
            p = subprocess.Popen(
                [self.tarpm, "-x", opt, "", "-f", self.tarpm],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()
            self.assertEqual(p.returncode, 1)
            s = "unable to canonicalize"
            self.assertNotEqual(err.decode("utf-8").find(s), -1, opt)


# Verify a later argument is not read as tar style option letters
class TarpmShortLettersOnlyFirst(TestUnpackRPM):
    def runTest(self):
        self.rpm.do_make()
        pkg = self.rpm.get_built_rpm(get_expected_arch())

        # "cx" would ask for two modes if it were read as letters
        p = subprocess.Popen(
            [self.tarpm, "-t", "-f", pkg, "cx", "extra"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0, err.decode("utf-8"))

        # the bundled form still works as the first argument
        dest = os.path.join(self.output_dir, "out")
        p = subprocess.Popen(
            [self.tarpm, "xnf", pkg, "-O", dest],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0, err.decode("utf-8"))
        self.assertTrue(os.path.isfile(os.path.join(dest, "header.json")))
