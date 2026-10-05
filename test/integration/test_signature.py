#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import os
import subprocess
import rpmfluff
from baseclass import NAME, TestUnpackSRPM, TestUnpackRPM

# Every signature header tag tarpm writes by name, and the tag number
# each name stands for.  See enum rpmSigTag_e in include/rpm/rpmtag.h
# in the rpm source.
SIGTAGS = {
    "Headersignatures": 62,
    "Headerimmutable": 63,
    "Badsha1_1": 264,
    "Badsha1_2": 265,
    "Pubkeys": 266,
    "Dsa": 267,
    "Rsa": 268,
    "Sha1": 269,
    "Longsize": 270,
    "Longarchivesize": 271,
    "Sha256": 273,
    "Filesignatures": 274,
    "Filesignaturelength": 275,
    "Veritysignatures": 276,
    "Veritysignaturealgo": 277,
    "Openpgp": 278,
    "Sha3_256": 279,
    "Reserved": 999,
    "Size": 1000,
    "Lemd5_1": 1001,
    "Pgp": 1002,
    "Lemd5_2": 1003,
    "Md5": 1004,
    "Gpg": 1005,
    "Pgp5": 1006,
    "Payloadsize": 1007,
    "Reservedspace": 1008,
}

# The signature header tags rpmsign writes and the type each one
# carries.  tarpm reads these out of a package it extracts, but it
# cannot make them without the private signing key, so a package it
# creates has to come out without them.  See RPMSIGN_SIGNATURE_TAGS
# in include/constants.h.
RPMSIGN_TAGS = {
    "Dsa": "binary blob",
    "Rsa": "binary blob",
    "Filesignatures": "string array",
    "Filesignaturelength": "int32",
    "Veritysignatures": "string array",
    "Veritysignaturealgo": "int32",
    "Openpgp": "string array",
    "Pgp": "binary blob",
    "Gpg": "binary blob",
    "Pgp5": "binary blob",
}


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
                    elif t not in SIGTAGS:
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

                # the tags come out in ascending signature tag order
                numbers = [SIGTAGS[tag["tag"]] for tag in signature[key]]
                self.assertEqual(numbers, sorted(numbers))


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
                    elif t not in SIGTAGS:
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

                # the tags come out in ascending signature tag order
                numbers = [SIGTAGS[tag["tag"]] for tag in signature[key]]
                self.assertEqual(numbers, sorted(numbers))


class VerifyCreateDropsRpmsignTags(TestUnpackRPM):
    """
    The signature tags rpmsign owns show up in signature.json when we
    extract a package, but a package tarpm creates has to come out
    without them.
    """

    def setUp(self):
        super().setUp()

        # creating a package needs something in the payload
        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # put one of every tag rpmsign owns in the extracted signature
        f = open(self.signature)
        signature = json.load(f)
        f.close()

        for tag, tagtype in RPMSIGN_TAGS.items():
            if tagtype == "int32":
                value = 73
            elif tagtype == "string array":
                value = ["bm90IGEgc2lnbmF0dXJl"]
            else:
                value = "bm90IGEgc2lnbmF0dXJl"

            signature["tags"].append({"tag": tag, "type": tagtype, "value": value})

        f = open(self.signature, "w")
        json.dump(signature, f)
        f.close()

        # create a new package from what we extracted
        created = os.path.join(self.output_dir, "created.rpm")
        args = [self.tarpm, "-c", "-f", created, self.output_dir]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode())

        # and read the signature back out of the new package
        again = os.path.join(self.output_dir, "again")
        os.makedirs(again)

        args = [self.tarpm, "-x", "-f", created, "-O", again]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, msg=err.decode())

        f = open(os.path.join(again, "signature.json"))
        signature = json.load(f)
        f.close()

        tags = [tag["tag"] for tag in signature["tags"]]

        # none of the tags rpmsign owns made it in to the new package
        for tag in RPMSIGN_TAGS:
            self.assertFalse(tag in tags, msg="%s is in the new signature" % tag)

        # the tags tarpm writes itself are all still there
        for tag in ["Sha1", "Sha256", "Size", "Md5", "Payloadsize"]:
            m = "%s is missing from the new signature" % tag
            self.assertTrue(tag in tags, msg=m)
