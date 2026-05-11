#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
from packaging.version import parse as parse_version
from baseclass import NAME, VER, REL
from baseclass import TestUnpackSRPM, TestUnpackRPM


class VerifyLeadExtractSRPM(TestUnpackSRPM):
    def runTest(self):
        super().runTest()

        f = open(self.lead)
        lead = json.load(f)
        f.close()

        for key in ["lead magic", "version", "type", "name", "architecture", "os", "signature type"]:
            self.assertTrue(key in lead.keys())

            if key == "lead magic":
                self.assertEqual(lead[key], "0xEDABEEDB")
            elif key == "version":
                self.assertTrue(parse_version(lead[key]) >= parse_version("3.0"))
            elif key == "type":
                self.assertEqual(lead[key], "source")
            elif key == "name":
                self.assertEqual(lead[key], "%s-%s-%s" % (NAME, VER, REL))
            elif key == "architecture":
                self.assertEqual(lead[key], 0)
            elif key == "os":
                self.assertEqual(lead[key], 0)
            elif key == "signature type":
                self.assertEqual(lead[key], 5)


class VerifyLeadExtractSRPM(TestUnpackRPM):
    def runTest(self):
        super().runTest()

        f = open(self.lead)
        lead = json.load(f)
        f.close()

        for key in ["lead magic", "version", "type", "name", "architecture", "os", "signature type"]:
            self.assertTrue(key in lead.keys())

            if key == "lead magic":
                self.assertEqual(lead[key], "0xEDABEEDB")
            elif key == "version":
                self.assertTrue(parse_version(lead[key]) >= parse_version("3.0"))
            elif key == "type":
                self.assertEqual(lead[key], "binary")
            elif key == "name":
                self.assertEqual(lead[key], "%s-%s-%s" % (NAME, VER, REL))
            elif key == "architecture":
                self.assertEqual(lead[key], 0)
            elif key == "os":
                self.assertEqual(lead[key], 0)
            elif key == "signature type":
                self.assertEqual(lead[key], 5)
