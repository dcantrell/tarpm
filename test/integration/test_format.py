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

# The main header tags only one of the two formats carries.  See the
# RPMFORMAT_V*_ONLY_HEADER_TAGS groups in include/constants.h.
V4_ONLY_TAGS = [1009, 1028, 1046, 1141, 1142, 5090, 5091, 5093]
V6_ONLY_TAGS = [5112, 5113, 5114, 5115, 5116, 5120, 5121, 5122, 5123, 5124]

# The size tags in their 32 bit and their 64 bit spelling
SIZE_TAGS = [1009, 1028]
LONG_SIZE_TAGS = [5008, 5009]

# The lead major version each format carries.  rpmLeadFromHeader() in
# lib/rpmlead.cc in the rpm source goes by whether the main header
# carries RPMTAG_RPMFORMAT.
V4_LEAD_MAJOR = 3
V6_LEAD_MAJOR = 4

# The compressor rpm picks from v6 on
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


def size_values(pkg, tag, width):
    """Return the numbers a size tag records in the main header of an RPM"""
    entries, data = main_header(pkg)

    for tagnum, tagtype, offset, count in entries:
        if tagnum == tag:
            fmt = ">%d%s" % (count, "I" if width == 4 else "Q")
            return list(struct.unpack_from(fmt, data, offset))

    return []


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


def add_string_array_tag(path, name, values):
    """Add one tag holding an array of strings to a header.json"""
    f = open(path)
    header = json.load(f)
    f.close()

    header["tags"].append({"tag": name, "type": "string array", "value": values})

    f = open(path, "w")
    json.dump(header, f)
    f.close()

    return


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
    """A v4 package carries the payload digest tags rpm writes for v4"""

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

        # nothing only a v6 package carries
        for tag in [5112, 5113, 5114, 5121, 5122, 5123, 5124]:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        # and the lead says v4
        self.assertEqual(lead_major(created), V4_LEAD_MAJOR)


class VerifyCreateFormatSixHeader(TestUnpackRPM):
    """A v6 package carries the format tag and the v6 digests"""

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

        # the digest algorithm tag belongs to v4 only
        self.assertFalse(5093 in tags, msg="main header carries 5093")

        # and the lead says v6
        self.assertEqual(lead_major(created), V6_LEAD_MAJOR)


class VerifyCreateFormatSixRpmformat(TestUnpackRPM):
    """The format tag of a v6 package names the format"""

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
    """A v6 package is compressed the way rpm compresses one"""

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
    """A v4 package keeps the compressor the metadata names"""

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
    The payload digests of a v6 package cover the payload, and
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
    Writing a v4 package from a v6 one takes the tags only
    v6 owns back out again.
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

        # extracting the v6 package gives us its v6 tags
        six_dir = os.path.join(self.output_dir, "six")
        os.makedirs(six_dir)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", six, "-O", six_dir])
        self.assertEqual(rc, 0, msg=err)

        # writing it back out as v4 leaves none of them behind
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


class VerifyCreateFormatFourDropsFormatTags(TestUnpackRPM):
    """
    Writing a v6 package from a v4 one takes the tags only
    v4 owns back out again.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        # extracting the v4 package gives us its v4 tags
        four_dir = os.path.join(self.output_dir, "four")
        os.makedirs(four_dir)
        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", four, "-O", four_dir])
        self.assertEqual(rc, 0, msg=err)

        # writing it back out as v6 leaves none of them behind
        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "6", "-f", six, four_dir])
        self.assertEqual(rc, 0, msg=err)

        entries, data = main_header(six)
        tags = tag_numbers(entries)

        for tag in V4_ONLY_TAGS:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        for tag in V6_HEADER_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        self.assertEqual(lead_major(six), V6_LEAD_MAJOR)


class VerifyCreateFormatFileSignatures(TestUnpackRPM):
    """
    A signed package keeps its file signatures in the main header,
    which rpm(8) only lets a v4 package do.
    """

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        # the tag rpmsign(1) leaves in the main header of a v4 package
        add_string_array_tag(self.header, "Filesignatures", ["0302", "0302"])

        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", six, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        entries, data = main_header(four)
        self.assertTrue(5090 in tag_numbers(entries), msg="main header is missing 5090")

        entries, data = main_header(six)
        self.assertFalse(5090 in tag_numbers(entries), msg="main header carries 5090")

        # rpm(8) turns away a package that gets this wrong
        for pkg in [four, six]:
            rc, out, err = run_tarpm(
                ["rpm", "--define", "_pkgverify_level digest", "-K", pkg]
            )
            self.assertEqual(rc, 0, msg=out + err)


class VerifyCreateFormatFourSizeTags(TestUnpackRPM):
    """A v4 package counts its sizes in the 32 bit size tags"""

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

        for tag in SIZE_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        for tag in LONG_SIZE_TAGS:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)


class VerifyCreateFormatSixSizeTags(TestUnpackRPM):
    """A v6 package counts its sizes in the 64 bit size tags"""

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

        for tag in LONG_SIZE_TAGS:
            self.assertTrue(tag in tags, msg="main header is missing %d" % tag)

        for tag in SIZE_TAGS:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)


class VerifyCreateFormatSizesMatch(TestUnpackRPM):
    """The two spellings of the size tags count the same bytes"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", six, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        # the installed size and then the size of every file
        self.assertEqual(size_values(four, 1009, 4), size_values(six, 5009, 8))
        self.assertEqual(size_values(four, 1028, 4), size_values(six, 5008, 8))
