#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import subprocess

from baseclass import RequiresTarpm


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
