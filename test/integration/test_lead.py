#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
from baseclass import NAME, VER, REL
from baseclass import LEAD_CARRIES_ARCH_OS
from baseclass import TestUnpackSRPM, TestUnpackRPM


def parse_version(v):
    """Simple version parser for comparing lead version field"""
    try:
        return tuple(int(x) for x in str(v).split(".")[:2])
    except (ValueError, AttributeError):
        return (0, 0)


class VerifyLeadExtractSRPM(TestUnpackSRPM):
    def runTest(self):
        super().runTest()

        f = open(self.lead)
        lead = json.load(f)
        f.close()

        for key in [
            "lead magic",
            "version",
            "type",
            "name",
            "architecture",
            "os",
            "signature type",
        ]:
            self.assertTrue(key in lead.keys())

            if key == "lead magic":
                self.assertEqual(lead[key], "0xEDABEEDB")
            elif key == "version":
                self.assertTrue(parse_version(lead[key]) >= parse_version("3.0"))
            elif key == "type":
                self.assertEqual(lead[key], "source")
            elif key == "name":
                self.assertEqual(lead[key], "%s-%s-%s" % (NAME, VER, REL))
            elif key in ["architecture", "os"]:
                # rpmLeadFromHeader() read both off the machine doing
                # the build until 6.0.0, which leaves them zero.  The
                # number an older rpm writes depends on the arch, so
                # only check that it said something.
                if LEAD_CARRIES_ARCH_OS:
                    self.assertTrue(isinstance(lead[key], int))
                else:
                    self.assertEqual(lead[key], 0)
            elif key == "signature type":
                self.assertEqual(lead[key], 5)


class VerifyLeadExtractRPM(TestUnpackRPM):
    def runTest(self):
        super().runTest()

        f = open(self.lead)
        lead = json.load(f)
        f.close()

        for key in [
            "lead magic",
            "version",
            "type",
            "name",
            "architecture",
            "os",
            "signature type",
        ]:
            self.assertTrue(key in lead.keys())

            if key == "lead magic":
                self.assertEqual(lead[key], "0xEDABEEDB")
            elif key == "version":
                self.assertTrue(parse_version(lead[key]) >= parse_version("3.0"))
            elif key == "type":
                self.assertEqual(lead[key], "binary")
            elif key == "name":
                self.assertEqual(lead[key], "%s-%s-%s" % (NAME, VER, REL))
            elif key in ["architecture", "os"]:
                # rpmLeadFromHeader() read both off the machine doing
                # the build until 6.0.0, which leaves them zero.  The
                # number an older rpm writes depends on the arch, so
                # only check that it said something.
                if LEAD_CARRIES_ARCH_OS:
                    self.assertTrue(isinstance(lead[key], int))
                else:
                    self.assertEqual(lead[key], 0)
            elif key == "signature type":
                self.assertEqual(lead[key], 5)
