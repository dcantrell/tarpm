#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
from baseclass import TestUnpackSRPM, TestUnpackRPM


class VerifySignatureExtractSRPM(TestUnpackSRPM):
    def runTest(self):
        super().runTest()

        f = open(self.signature)
        signature = json.load(f)
        f.close()

        # Check main signature fields
        for key in ["magic", "reserved", "index entries", "index size (bytes)", "data size (bytes)", "header size (bytes)", "tags"]:
            self.assertTrue(key in signature.keys())

            if key == "magic":
                self.assertEqual(signature[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(signature[key], "0000")
            elif key == "index entries":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "index size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "data size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "header size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "tags":
                self.assertTrue(isinstance(signature[key], list))
                self.assertTrue(len(signature[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in signature[key]:
                    self.assertTrue("name" in tag.keys())
                    self.assertTrue("number" in tag.keys())
                    self.assertTrue("type" in tag.keys())
                    self.assertTrue("offset" in tag.keys())
                    self.assertTrue("count" in tag.keys())


class VerifySignatureExtractRPM(TestUnpackRPM):
    def runTest(self):
        super().runTest()

        f = open(self.signature)
        signature = json.load(f)
        f.close()

        # Check main signature fields
        for key in ["magic", "reserved", "index entries", "index size (bytes)", "data size (bytes)", "header size (bytes)", "tags"]:
            self.assertTrue(key in signature.keys())

            if key == "magic":
                self.assertEqual(signature[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(signature[key], "0000")
            elif key == "index entries":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "index size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "data size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "header size (bytes)":
                self.assertTrue(int(signature[key]) > 0)
            elif key == "tags":
                self.assertTrue(isinstance(signature[key], list))
                self.assertTrue(len(signature[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in signature[key]:
                    self.assertTrue("name" in tag.keys())
                    self.assertTrue("number" in tag.keys())
                    self.assertTrue("type" in tag.keys())
                    self.assertTrue("offset" in tag.keys())
                    self.assertTrue("count" in tag.keys())
