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
        for key in [
            "magic",
            "reserved",
            "tags",
        ]:
            self.assertTrue(key in signature.keys())

            if key == "magic":
                self.assertEqual(signature[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(signature[key], "0000")
            elif key == "tags":
                self.assertTrue(isinstance(signature[key], list))
                self.assertTrue(len(signature[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in signature[key]:
                    self.assertTrue("tag" in tag.keys())
                    self.assertTrue("type" in tag.keys())

                    t = tag["tag"]

                    if t == "Headersignatures":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                        self.assertTrue("trailer" in tag.keys())
                    elif t == "Sha1":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) == 40)
                    elif t == "Sha256":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) == 64)
                    elif t == "Size":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] > 1)
                    elif t == "Md5":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Payloadsize":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] > 1)
                    elif t == "Reservedspace":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 5590)
                    else:
                        # unknown tag is now appearing, write it out

                        # To see what it is, uncomment this block and
                        # run the test suite again.  The data will be
                        # in /tmp/rpm-signature-tag.
                        #
                        # f = open("/tmp/rpm-signature-tag", "w+")
                        # f.write(str(tag))
                        # f.close()

                        m = "Unknown tag found in signature: %s" % t
                        self.fail(msg=m)


class VerifySignatureExtractRPM(TestUnpackRPM):
    def runTest(self):
        super().runTest()

        f = open(self.signature)
        signature = json.load(f)
        f.close()

        # Check main signature fields
        for key in [
            "magic",
            "reserved",
            "tags",
        ]:
            self.assertTrue(key in signature.keys())

            if key == "magic":
                self.assertEqual(signature[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(signature[key], "0000")
            elif key == "tags":
                self.assertTrue(isinstance(signature[key], list))
                self.assertTrue(len(signature[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in signature[key]:
                    self.assertTrue("tag" in tag.keys())
                    self.assertTrue("type" in tag.keys())

                    t = tag["tag"]

                    if t == "Headersignatures":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Sha1":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) == 40)
                    elif t == "Sha256":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) == 64)
                    elif t == "Size":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] > 1)
                    elif t == "Md5":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Payloadsize":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] > 1)
                    elif t == "Reservedspace":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 5590)
                    else:
                        # unknown tag is now appearing, write it out

                        # To see what it is, uncomment this block and
                        # run the test suite again.  The data will be
                        # in /tmp/rpm-signature-tag.
                        #
                        # f = open("/tmp/rpm-signature-tag", "w+")
                        # f.write(str(tag))
                        # f.close()

                        m = "Unknown tag found in signature: %s" % t
                        self.fail(msg=m)
