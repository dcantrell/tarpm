#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import os
import shutil
import subprocess
import unittest

MANPAGE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "doc",
    "tarpm.1",
)


# Verify groff renders the man page without any warnings.  -z throws
# the output away and -ww -b turns on every warning with a backtrace.
class ManPageGroffClean(unittest.TestCase):
    def runTest(self):
        groff = shutil.which("groff")

        if groff is None:
            self.skipTest("groff not found")

        for device in ["utf8", "ascii", "ps"]:
            p = subprocess.Popen(
                [groff, "-man", "-T" + device, "-ww", "-b", "-z", MANPAGE],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
            out, err = p.communicate()

            self.assertEqual(p.returncode, 0, device)
            self.assertEqual(err.decode().strip(), "", device)


# Verify mandoc parses the man page with no warnings.  mandoc is
# stricter than groff and is what the BSDs and Alpine use to format
# man pages.
class ManPageMandocClean(unittest.TestCase):
    def runTest(self):
        mandoc = shutil.which("mandoc")

        if mandoc is None:
            self.skipTest("mandoc not found")

        p = subprocess.Popen(
            [mandoc, "-Tlint", "-Wall", MANPAGE],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        out, err = p.communicate()

        self.assertEqual(out.decode().strip(), "")
        self.assertEqual(p.returncode, 0)
