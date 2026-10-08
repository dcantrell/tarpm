#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import hashlib
import json
import os
import struct
import subprocess
import rpmfluff
from baseclass import NAME, TestUnpackSRPM, TestUnpackRPM

# The signature header tags rpm writes by default, one set per RPM
# format.  rpmGenerateSignature() in lib/signature.cc in the rpm
# source puts these in and nothing else.  See the
# RPMFORMAT_V*_SIGNATURE_TAGS groups in include/constants.h.
V4_SIGNATURE_TAGS = [62, 269, 273, 1000, 1004, 1007, 1008]
V6_SIGNATURE_TAGS = [62, 273, 279, 999]

# The space rpm reserves in the signature header for rpmsign
RESERVED_SIZE = 4128

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


def run_tarpm(args):
    """Run tarpm with the given arguments and return (returncode, stdout, stderr)"""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate()

    return (proc.returncode, out.decode(), err.decode())


def read_header(f):
    """Read one RPM header and return its index entries and its data"""
    magic, reserved, nentries, nbytes = struct.unpack(">IIII", f.read(16))
    entries = [struct.unpack(">IIiI", f.read(16)) for i in range(nentries)]
    data = f.read(nbytes)

    return (entries, data)


def signature_header(pkg):
    """Return the index entries of the signature header of an RPM"""
    f = open(pkg, "rb")
    f.seek(96)
    entries, data = read_header(f)
    f.close()

    return entries


def main_header_bytes(pkg):
    """Return the main header of an RPM the way the digests see it"""
    f = open(pkg, "rb")
    f.seek(96)
    entries, data = read_header(f)

    # the signature header is padded out to an 8 byte boundary
    f.read((8 - (len(data) % 8)) % 8)

    start = f.tell()
    read_header(f)
    end = f.tell()

    f.seek(start)
    header = f.read(end - start)
    f.close()

    return header


def tag_numbers(entries):
    """Return the tag numbers of a set of header index entries"""
    return [tag for (tag, tagtype, offset, count) in entries]


def tag_count(entries, tag):
    """Return the count field of one header index entry"""
    for entry in entries:
        if entry[0] == tag:
            return entry[3]

    return None


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

                    # signature header tags are read-only
                    m = "%s is not marked read-only" % t
                    self.assertEqual(tag.get("read-only"), "true", msg=m)

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

                    # signature header tags are read-only
                    m = "%s is not marked read-only" % t
                    self.assertEqual(tag.get("read-only"), "true", msg=m)

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

        # The tags tarpm writes itself are all still there.  Being
        # marked read-only tells the user not to edit a tag, it does
        # not keep the tag out of a package we create.
        for tag in ["Sha1", "Sha256", "Size", "Md5", "Payloadsize"]:
            m = "%s is missing from the new signature" % tag
            self.assertTrue(tag in tags, msg=m)

        for tag in signature["tags"]:
            m = "%s is not marked read-only" % tag["tag"]
            self.assertEqual(tag.get("read-only"), "true", msg=m)


class VerifyCreateWritesDefaultSignature(TestUnpackRPM):
    """
    We build the signature header ourselves rather than read it back
    from signature.json, so a package we create carries the tags rpm
    writes by default and nothing else.
    """

    def setUp(self):
        super().setUp()

        # creating a package needs something in the payload
        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", created, self.output_dir])
        self.assertEqual(rc, 0, msg=err)

        entries = signature_header(created)
        self.assertEqual(tag_numbers(entries), V4_SIGNATURE_TAGS)
        self.assertEqual(tag_count(entries, 1008), RESERVED_SIZE)


class VerifyCreateWithoutSignatureJson(TestUnpackRPM):
    """
    signature.json is not read when we create a package, so the
    package comes out the same without it.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())
        os.unlink(self.signature)

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", created, self.output_dir])
        self.assertEqual(rc, 0, msg=err)

        f = open(original, "rb")
        one = f.read()
        f.close()

        f = open(created, "rb")
        two = f.read()
        f.close()

        self.assertEqual(one, two, "the package came out different")


class VerifyCreateIgnoresEditedSignature(TestUnpackRPM):
    """
    Nothing in signature.json reaches a package we create, so editing
    it changes nothing.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # rewrite the signature with junk tags and bogus digests
        f = open(self.signature)
        signature = json.load(f)
        f.close()

        for tag in signature["tags"]:
            if tag["tag"] in ["Sha1", "Sha256"]:
                tag["value"] = "0" * len(tag["value"])
            elif tag["tag"] in ["Size", "Payloadsize"]:
                tag["value"] = 47

        signature["tags"].append(
            {"tag": "Lemd5_1", "type": "binary blob", "value": "bm90IGEgc2ln"}
        )

        f = open(self.signature, "w")
        json.dump(signature, f)
        f.close()

        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())
        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-f", created, self.output_dir])
        self.assertEqual(rc, 0, msg=err)

        f = open(original, "rb")
        one = f.read()
        f.close()

        f = open(created, "rb")
        two = f.read()
        f.close()

        self.assertEqual(one, two, "the edited signature reached the package")


class VerifyCreateFormatFour(TestUnpackRPM):
    """Asking for v4 gives us the same package the default does"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        original = self.rpm.get_built_rpm(rpmfluff.utils.get_expected_arch())
        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "--format", "4", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        entries = signature_header(created)
        self.assertEqual(tag_numbers(entries), V4_SIGNATURE_TAGS)

        f = open(original, "rb")
        one = f.read()
        f.close()

        f = open(created, "rb")
        two = f.read()
        f.close()

        self.assertEqual(one, two, "the package came out different")


class VerifyCreateFormatSix(TestUnpackRPM):
    """
    Asking for v6 gives us the signature header rpm writes for
    that format, which is digests of the main header and nothing else.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        entries = signature_header(created)
        self.assertEqual(tag_numbers(entries), V6_SIGNATURE_TAGS)
        self.assertEqual(tag_count(entries, 999), RESERVED_SIZE)

        # the digests cover the main header
        again = os.path.join(self.output_dir, "again")
        os.makedirs(again)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", created, "-O", again])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(again, "signature.json"))
        signature = json.load(f)
        f.close()

        digests = dict(
            (tag["tag"], tag["value"])
            for tag in signature["tags"]
            if tag["tag"] in ["Sha256", "Sha3_256"]
        )

        header = main_header_bytes(created)
        self.assertEqual(digests["Sha256"], hashlib.sha256(header).hexdigest())
        self.assertEqual(digests["Sha3_256"], hashlib.sha3_256(header).hexdigest())


class VerifyCreateRejectsUnknownFormat(TestUnpackRPM):
    """The only formats we know how to write are 4 and 6"""

    def runTest(self):
        super().runTest()

        created = os.path.join(self.output_dir, "created.rpm")

        for fmt in ["3", "5", "7", "six", ""]:
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", fmt, "-f", created, self.output_dir]
            )
            self.assertNotEqual(rc, 0, msg="-F %s was accepted" % fmt)
            self.assertTrue("-F must be 4 or 6" in err, msg=err)
            self.assertFalse(os.path.exists(created), "a package was written anyway")


class VerifyFormatRejectedOnExtract(TestUnpackRPM):
    """The format option only means something when we create a package"""

    def setUp(self):
        super().setUp()

        self.opts = ["-F", "6"]
        self.exitcode = 1

    def runTest(self):
        super().runTest()

        err = self.err.decode()
        m = "-F may only be used when creating an RPM"
        self.assertTrue(m in err, msg=err)


class VerifySignaturePathIgnoredOnCreate(TestUnpackRPM):
    """We generate the signature header, so -S has nothing to say on create"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [
                self.tarpm,
                "-c",
                "-S",
                self.signature,
                "-f",
                created,
                self.output_dir,
            ]
        )
        self.assertEqual(rc, 0, msg=err)
        self.assertTrue("-S is ignored when creating an RPM" in err, msg=err)

        entries = signature_header(created)
        self.assertEqual(tag_numbers(entries), V4_SIGNATURE_TAGS)
