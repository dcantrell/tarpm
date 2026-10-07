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
from baseclass import NAME, TestUnpackRPM

# The main header tags rpm writes for the payload, one set per RPM
# format.  writeRPM() in build/pack.cc in the rpm source puts these in.
# See the RPMFORMAT_V*_HEADER_TAGS groups in include/constants.h.
V4_HEADER_TAGS = [5092, 5093, 5097]
V6_HEADER_TAGS = [5092, 5097, 5112, 5113, 5114, 5121, 5122, 5123, 5124]

# The lead major version each format carries.  rpmLeadFromHeader() in
# lib/rpmlead.cc in the rpm source goes by whether the main header
# carries RPMTAG_RPMFORMAT.
V4_LEAD_MAJOR = 3
V6_LEAD_MAJOR = 4

# The compressor rpm picks from format 6 on
V6_COMPRESSOR = "zstd"
V6_COMPRESSOR_LEVEL = "19"


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


def lead_major(pkg):
    """Return the major version of the lead of an RPM"""
    f = open(pkg, "rb")
    lead = f.read(96)
    f.close()

    return lead[4]


def main_header(pkg):
    """Return the index entries and data of the main header of an RPM"""
    f = open(pkg, "rb")
    f.seek(96)
    entries, data = read_header(f)

    # the signature header is padded out to an 8 byte boundary
    f.read((8 - (len(data) % 8)) % 8)

    entries, data = read_header(f)
    f.close()

    return (entries, data)


def tag_numbers(entries):
    """Return the tag numbers of a set of header index entries"""
    return [tag for (tag, tagtype, offset, count) in entries]


def extracted_header(test, pkg, where):
    """Extract an RPM and return the header.json it writes"""
    out_dir = os.path.join(test.output_dir, where)
    os.makedirs(out_dir)

    rc, out, err = run_tarpm([test.tarpm, "-x", "-f", pkg, "-O", out_dir])
    test.assertEqual(rc, 0, msg=err)

    f = open(os.path.join(out_dir, "header.json"))
    header = json.load(f)
    f.close()

    return header


def header_tags(header):
    """Return the tags of a header.json keyed by tag name"""
    return dict((tag["tag"], tag.get("value")) for tag in header["tags"])


def set_compressor(path, compressor, level):
    """Rewrite the payload compressor a header.json names"""
    f = open(path)
    header = json.load(f)
    f.close()

    for tag in header["tags"]:
        if tag["tag"] == "Payloadcompressor":
            tag["value"] = compressor
        elif tag["tag"] == "Payloadflags":
            tag["value"] = level

    f = open(path, "w")
    json.dump(header, f)
    f.close()

    return


class VerifyCreateFormatFourHeader(TestUnpackRPM):
    """A format 4 package carries the payload digest tags rpm writes for format 4"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        entries, data = main_header(created)
        tags = tag_numbers(entries)

        for tag in V4_HEADER_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        # nothing only a format 6 package carries
        for tag in [5112, 5113, 5114, 5121, 5122, 5123, 5124]:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        # and the lead says format 4
        self.assertEqual(lead_major(created), V4_LEAD_MAJOR)


class VerifyCreateFormatSixHeader(TestUnpackRPM):
    """A format 6 package carries the format tag and the format 6 digests"""

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

        entries, data = main_header(created)
        tags = tag_numbers(entries)

        for tag in V6_HEADER_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        # the digest algorithm tag belongs to format 4 only
        self.assertFalse(5093 in tags, msg="main header carries 5093")

        # and the lead says format 6
        self.assertEqual(lead_major(created), V6_LEAD_MAJOR)


class VerifyCreateFormatSixRpmformat(TestUnpackRPM):
    """The format tag of a format 6 package names the format"""

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

        tags = header_tags(extracted_header(self, created, "again"))
        self.assertEqual(tags["Rpmformat"], 6)


class VerifyCreateFormatSixCompression(TestUnpackRPM):
    """A format 6 package is compressed the way rpm compresses one"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # start from a package compressed some other way
        set_compressor(self.header, "gzip", "9")

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        tags = header_tags(extracted_header(self, created, "again"))
        self.assertEqual(tags["Payloadcompressor"], V6_COMPRESSOR)
        self.assertEqual(tags["Payloadflags"], V6_COMPRESSOR_LEVEL)


class VerifyCreateFormatFourCompression(TestUnpackRPM):
    """A format 4 package keeps the compressor the metadata names"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        set_compressor(self.header, "gzip", "9")

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        tags = header_tags(extracted_header(self, created, "again"))
        self.assertEqual(tags["Payloadcompressor"], "gzip")
        self.assertEqual(tags["Payloadflags"], "9")


class VerifyCreateFormatSixPayloadDigests(TestUnpackRPM):
    """
    The payload digests of a format 6 package cover the payload, and
    the ALT ones cover the same payload with the compression taken off.
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

        # the payload is whatever follows the main header
        f = open(created, "rb")
        f.seek(96)
        entries, data = read_header(f)
        f.read((8 - (len(data) % 8)) % 8)
        read_header(f)
        payload = f.read()
        f.close()

        tags = header_tags(extracted_header(self, created, "again"))

        self.assertEqual(tags["Payloadsha256"][0], hashlib.sha256(payload).hexdigest())
        self.assertEqual(tags["Payloadsha512"], hashlib.sha512(payload).hexdigest())
        self.assertEqual(tags["Payloadsha3_256"], hashlib.sha3_256(payload).hexdigest())
        self.assertEqual(tags["Payloadsize"], len(payload))

        # the uncompressed cpio stream is bigger and digests differently
        self.assertTrue(tags["Payloadsizealt"] > tags["Payloadsize"])
        self.assertNotEqual(tags["Payloadsha256alt"][0], tags["Payloadsha256"][0])
        self.assertNotEqual(tags["Payloadsha512alt"], tags["Payloadsha512"])
        self.assertNotEqual(tags["Payloadsha3_256alt"], tags["Payloadsha3_256"])


class VerifyCreateFormatSixDropsFormatTags(TestUnpackRPM):
    """
    Writing a format 4 package from a format 6 one takes the tags only
    format 6 owns back out again.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", six, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        # extracting the format 6 package gives us its format 6 tags
        six_dir = os.path.join(self.output_dir, "six")
        os.makedirs(six_dir)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", six, "-O", six_dir])
        self.assertEqual(rc, 0, msg=err)

        # writing it back out as format 4 leaves none of them behind
        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "4", "-f", four, six_dir])
        self.assertEqual(rc, 0, msg=err)

        entries, data = main_header(four)
        tags = tag_numbers(entries)

        for tag in [5112, 5113, 5114, 5121, 5122, 5123, 5124]:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        for tag in V4_HEADER_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        self.assertEqual(lead_major(four), V4_LEAD_MAJOR)
