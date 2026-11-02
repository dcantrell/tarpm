#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import hashlib
import json
import os
import struct
import subprocess
import unittest
import rpmfluff
import rpm
from baseclass import NAME, TestUnpackRPM
from baseclass import HAVE_RPM_FORMAT_SIX, rpm_at_least

# The main header tags rpm writes for the payload, one set per RPM
# format.  writeRPM() in build/pack.cc in the rpm source puts these in.
# See the RPMFORMAT_V*_HEADER_TAGS groups in include/constants.h.

# The header tags we *hope* we can get in a v4 format header.
_v4_tags = [5092, 5093, 5097]

# Build V4_HEADER_TAGS dynamically to account for tags that don't
# exist in older RPM versions.
V4_HEADER_TAGS = [
    tag
    for tag in _v4_tags
    if rpm.tagnames.get(tag) and rpm.tagnames.get(tag) != "(unknown)"
]

# The header tags always present in v6 headers.
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

# The compressor rpm gives a v6 format binary package by default
V6_COMPRESSOR = "zstd"
V6_COMPRESSOR_LEVEL = "19"


def run_tarpm(args):
    """Run tarpm with the given arguments and return (returncode, stdout, stderr)"""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate()

    return (proc.returncode, out.decode(), err.decode())


def query(pkg, queryformat):
    """Run rpm -qp over a package and return what it printed"""
    proc = subprocess.Popen(
        ["rpm", "-qp", "--qf", queryformat, pkg],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = proc.communicate()

    return out.decode()


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


# The per-file signature tags and their companions in the signature
# header.  A main header may carry none of the verity ones.
FILE_SIGNATURE_TAGS = [274, 275, 276, 277]
MAIN_FILE_SIGNATURE_TAGS = [5090, 5091, 276, 277]

# Made up file and verity signatures.  rpm only checks that they
# decode, it needs the signing keys to check more.
FILE_SIGNATURE = "0302041234abcd"
VERITY_SIGNATURE = "dmVyaXR5IHNpZ25hdHVyZQ=="
VERITY_ALGO = 2

# A file signature too large for the reserved space rpm keeps in the
# signature header, which is 4128 bytes.
RESERVED_SIZE = 4128
LARGE_FILE_SIGNATURE = "03" + "ab" * RESERVED_SIZE

# The reserved space tag each format carries in its signature header
RESERVED_TAGS = {"4": 1008, "6": 999}

# rpm reads the verity signatures starting with 4.17.0
HAVE_VERITY = rpm_at_least("4.17.0")

# The formats this rpm can create packages in
FORMATS = ["4", "6"] if HAVE_RPM_FORMAT_SIX else ["4"]


def signature_header(pkg):
    """Return the index entries of the signature header of an RPM"""
    f = open(pkg, "rb")
    f.seek(96)
    entries, data = read_header(f)
    f.close()

    return entries


def signature_size(pkg):
    """Return the size of the signature header of an RPM, padding left out"""
    f = open(pkg, "rb")
    f.seek(96)
    entries, data = read_header(f)
    f.close()

    return 16 + 16 * len(entries) + len(data)


def entry_count(entries, tag):
    """Return the count field of one header index entry"""
    for entry in entries:
        if entry[0] == tag:
            return entry[3]

    return None


def sign_files(path, filename, signature=FILE_SIGNATURE):
    """Give one file in a header.json a file and a verity signature"""
    f = open(path)
    header = json.load(f)
    f.close()

    for entry in header["files"]:
        if entry["path"] == filename:
            entry["signature"] = signature
            entry["verity"] = VERITY_SIGNATURE

    header["tags"].append(
        {"tag": "Veritysignaturealgo", "type": "int32", "value": VERITY_ALGO}
    )

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


def drop_compressor(path):
    """Take the payload compressor and its level out of a header.json"""
    f = open(path)
    header = json.load(f)
    f.close()

    header["tags"] = [
        tag
        for tag in header["tags"]
        if tag["tag"] not in ["Payloadcompressor", "Payloadflags"]
    ]

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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyCreateFormatSixCompression(TestUnpackRPM):
    """A v6 format package keeps the compressor the metadata names"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        set_compressor(self.header, "gzip", "9")

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        tags = header_tags(extracted_header(self, created, "again"))
        self.assertEqual(tags["Payloadcompressor"], "gzip")
        self.assertEqual(tags["Payloadflags"], "9")


class VerifyCreateFormatFourCompression(TestUnpackRPM):
    """A v4 package keeps the compressor the metadata names"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        set_compressor(self.header, "zstd", "10")

        created = os.path.join(self.output_dir, "created.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", created, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        tags = header_tags(extracted_header(self, created, "again"))
        self.assertEqual(tags["Payloadcompressor"], "zstd")
        self.assertEqual(tags["Payloadflags"], "10")


class VerifyCreateDefaultCompression(TestUnpackRPM):
    """A binary package with no compressor named gets the one rpm picks"""

    def setUp(self):
        super().setUp()

        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file("/usr/share/doc/%s/README" % NAME, readme)

    def runTest(self):
        super().runTest()

        drop_compressor(self.header)

        # rpm gives a v4 format package gzip and a v6 format one zstd
        formats = {"4": ("gzip", "9")}

        if HAVE_RPM_FORMAT_SIX:
            formats["6"] = (V6_COMPRESSOR, V6_COMPRESSOR_LEVEL)

        for fmt, (compressor, level) in formats.items():
            created = os.path.join(self.output_dir, "created-%s.rpm" % fmt)
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", fmt, "-f", created, self.output_dir]
            )
            self.assertEqual(rc, 0, msg=err)

            tags = header_tags(extracted_header(self, created, "again-%s" % fmt))
            self.assertEqual(tags["Payloadcompressor"], compressor, msg=fmt)
            self.assertEqual(tags["Payloadflags"], level, msg=fmt)


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


class FileSignatures(object):
    """
    Mixin for the file signature tests.  The extracted tree gets a file
    and a verity signature on its one file.
    """

    def setUp(self):
        super().setUp()

        self.readme = "/usr/share/doc/%s/README" % NAME
        readme = rpmfluff.SourceFile("README", b"vaporware\n")
        self.rpm.add_installed_file(self.readme, readme)

    def runTest(self):
        super().runTest()

        sign_files(self.header, self.readme)

    def create(self, name, fmt, tree=None):
        """Create a package of the given format and return its path"""
        pkg = os.path.join(self.output_dir, name)
        tree = self.output_dir if tree is None else tree
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", fmt, "-f", pkg, tree])
        self.assertEqual(rc, 0, msg=err)

        # rpm(8) turns away a package that gets this wrong
        rc, out, err = run_tarpm(
            ["rpm", "--define", "_pkgverify_level digest", "-K", pkg]
        )
        self.assertEqual(rc, 0, msg=out + err)

        return pkg

    def extract(self, pkg, where):
        """Extract a package and return its header.json and signature.json"""
        out_dir = os.path.join(self.output_dir, where)
        os.makedirs(out_dir)

        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", pkg, "-O", out_dir])
        self.assertEqual(rc, 0, msg=err)

        f = open(os.path.join(out_dir, "header.json"))
        header = json.load(f)
        f.close()

        f = open(os.path.join(out_dir, "signature.json"))
        signature = json.load(f)
        f.close()

        return (out_dir, header, signature)

    def assertNoFileSignatures(self, pkg):
        """Check that no header of a package carries file signatures"""
        tags = tag_numbers(signature_header(pkg))

        for tag in FILE_SIGNATURE_TAGS:
            self.assertFalse(tag in tags, msg="signature header carries %d" % tag)

        entries, data = main_header(pkg)
        tags = tag_numbers(entries)

        for tag in MAIN_FILE_SIGNATURE_TAGS:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        return

    def assertFileSignatures(self, pkg, signature=FILE_SIGNATURE):
        """Check that a package carries the file signatures we gave it"""
        tags = tag_numbers(signature_header(pkg))

        for tag in FILE_SIGNATURE_TAGS:
            self.assertTrue(tag in tags, msg="signature header is missing %d" % tag)

        entries, data = main_header(pkg)
        tags = tag_numbers(entries)

        for tag in MAIN_FILE_SIGNATURE_TAGS:
            self.assertFalse(tag in tags, msg="main header carries %d" % tag)

        # rpm(8) reads them back for the file they sign
        qf = "[%{FILENAMES}|%{FILESIGNATURES}|%{VERITYSIGNATURES}\n]"
        lines = query(pkg, qf).splitlines()
        line = "%s|%s|%s" % (self.readme, signature, VERITY_SIGNATURE)
        self.assertTrue(line in lines, msg=lines)

        qf = "%{FILESIGNATURELENGTH}|%{VERITYSIGNATUREALGO}"
        self.assertEqual(query(pkg, qf), "%d|%d" % (len(signature) // 2, VERITY_ALGO))

        return

    def assertExtractedSignatures(self, pkg, where):
        """Check that extracting a package puts the signatures with the file"""
        out_dir, header, signature = self.extract(pkg, where)

        for entry in header["files"]:
            if entry["path"] == self.readme:
                self.assertEqual(entry.get("signature"), FILE_SIGNATURE)
                self.assertEqual(entry.get("verity"), VERITY_SIGNATURE)
            else:
                # an empty one gets no key
                self.assertFalse("signature" in entry, msg=entry["path"])
                self.assertFalse("verity" in entry, msg=entry["path"])

        self.assertEqual(header_tags(header).get("Veritysignaturealgo"), VERITY_ALGO)

        names = [tag["tag"] for tag in signature["tags"]]

        for name in [
            "Filesignatures",
            "Filesignaturelength",
            "Veritysignatures",
            "Veritysignaturealgo",
        ]:
            self.assertFalse(name in names, msg="signature.json carries %s" % name)

        return out_dir


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyCreateFormatSixFileSignatures(FileSignatures, TestUnpackRPM):
    """A v6 package keeps the file signatures in the signature header"""

    def runTest(self):
        super().runTest()

        six = self.create("six.rpm", "6")
        self.assertFileSignatures(six)
        self.assertExtractedSignatures(six, "again")


@unittest.skipUnless(HAVE_VERITY, "rpm is older than 4.17.0 and has no verity tags")
class VerifyCreateFormatFourFileSignatures(FileSignatures, TestUnpackRPM):
    """A v4 package keeps the file signatures in the signature header"""

    def runTest(self):
        super().runTest()

        four = self.create("four.rpm", "4")
        self.assertFileSignatures(four)
        self.assertExtractedSignatures(four, "again")


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyFormatRoundTripFileSignatures(FileSignatures, TestUnpackRPM):
    """The file signatures survive switching formats both ways"""

    def runTest(self):
        super().runTest()

        six = self.create("six.rpm", "6")
        six_dir = self.assertExtractedSignatures(six, "six")

        four = self.create("four.rpm", "4", six_dir)
        self.assertFileSignatures(four)
        four_dir = self.assertExtractedSignatures(four, "four")

        back = self.create("back.rpm", "6", four_dir)
        self.assertFileSignatures(back)


@unittest.skipUnless(HAVE_VERITY, "rpm is older than 4.17.0 and has no verity tags")
class VerifyCreateDropsChangedFileSignatures(FileSignatures, TestUnpackRPM):
    """A file whose contents changed loses its signatures"""

    def runTest(self):
        super().runTest()

        readme = os.path.join(self.output_dir, "payload", self.readme[1:])
        f = open(readme, "w")
        f.write("something else\n")
        f.close()

        for fmt in FORMATS:
            pkg = os.path.join(self.output_dir, "pkg%s.rpm" % fmt)
            rc, out, err = run_tarpm(
                [self.tarpm, "-c", "-F", fmt, "-f", pkg, self.output_dir]
            )
            self.assertEqual(rc, 0, msg=err)
            self.assertTrue("%s changed" % self.readme in err, msg=err)

            self.assertNoFileSignatures(pkg)


@unittest.skipUnless(HAVE_VERITY, "rpm is older than 4.17.0 and has no verity tags")
class VerifyCreateFileSignaturesKeepSignatureSize(FileSignatures, TestUnpackRPM):
    """
    The file signatures come out of the reserved space, so the
    signature header is the same size with them as without them.
    rpmsign(1) does the same.
    """

    def runTest(self):
        # the tree as extracted, before we sign anything
        TestUnpackRPM.runTest(self)

        for fmt in FORMATS:
            plain = self.create("plain%s.rpm" % fmt, fmt)

        sign_files(self.header, self.readme)

        for fmt in FORMATS:
            plain = os.path.join(self.output_dir, "plain%s.rpm" % fmt)
            signed = self.create("signed%s.rpm" % fmt, fmt)
            self.assertFileSignatures(signed)

            self.assertEqual(signature_size(signed), signature_size(plain))

            tag = RESERVED_TAGS[fmt]
            before = entry_count(signature_header(plain), tag)
            after = entry_count(signature_header(signed), tag)
            self.assertEqual(before, RESERVED_SIZE)
            self.assertTrue(after < before, msg="%d is not below %d" % (after, before))


@unittest.skipUnless(HAVE_VERITY, "rpm is older than 4.17.0 and has no verity tags")
class VerifyCreateFileSignaturesTooLarge(FileSignatures, TestUnpackRPM):
    """
    File signatures too large for the reserved space leave it alone and
    the signature header grows.  rpmsign(1) does the same.
    """

    def runTest(self):
        TestUnpackRPM.runTest(self)

        for fmt in FORMATS:
            plain = self.create("plain%s.rpm" % fmt, fmt)

        sign_files(self.header, self.readme, LARGE_FILE_SIGNATURE)

        for fmt in FORMATS:
            plain = os.path.join(self.output_dir, "plain%s.rpm" % fmt)
            signed = self.create("signed%s.rpm" % fmt, fmt)
            self.assertFileSignatures(signed, LARGE_FILE_SIGNATURE)

            self.assertTrue(signature_size(signed) > signature_size(plain))

            tag = RESERVED_TAGS[fmt]
            self.assertEqual(entry_count(signature_header(signed), tag), RESERVED_SIZE)


class VerifyCreateFormatFourMainHeaderFileSignatures(FileSignatures, TestUnpackRPM):
    """
    An older rpmsign(1) left the file signatures in the main header of
    a v4 package.  rpm(8) turns away a package with them in both
    headers, so the main header ones win.
    """

    def runTest(self):
        super().runTest()

        # one signature per file in the file list
        f = open(self.header)
        header = json.load(f)
        f.close()

        add_string_array_tag(
            self.header, "Filesignatures", ["0302"] * len(header["files"])
        )

        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)
        self.assertTrue("main header carries Filesignatures" in err, msg=err)

        entries, data = main_header(four)
        self.assertTrue(5090 in tag_numbers(entries))

        tags = tag_numbers(signature_header(four))
        self.assertFalse(274 in tags, msg="signature header carries 274")
        self.assertFalse(275 in tags, msg="signature header carries 275")

        rc, out, err = run_tarpm(
            ["rpm", "--define", "_pkgverify_level digest", "-K", four]
        )
        self.assertEqual(rc, 0, msg=out + err)


@unittest.skipUnless(HAVE_VERITY, "rpm is older than 4.17.0")
class VerifyExtractMainHeaderFileSignatures(FileSignatures, TestUnpackRPM):
    """
    The file signatures an older rpmsign(1) left in the main header of
    a v4 package go to the files array, so either format gets them.
    """

    def runTest(self):
        # the tree as extracted, before we sign anything
        TestUnpackRPM.runTest(self)

        f = open(self.header)
        header = json.load(f)
        f.close()

        signatures = [
            FILE_SIGNATURE if entry["path"] == self.readme else ""
            for entry in header["files"]
        ]
        add_string_array_tag(self.header, "Filesignatures", signatures)

        # a v4 package with the file signatures in its main header
        old = self.create("old.rpm", "4")
        entries, data = main_header(old)
        self.assertTrue(5090 in tag_numbers(entries))
        self.assertFalse(274 in tag_numbers(signature_header(old)))

        out_dir, header, signature = self.extract(old, "old")

        for entry in header["files"]:
            if entry["path"] == self.readme:
                self.assertEqual(entry.get("signature"), FILE_SIGNATURE)
            else:
                self.assertFalse("signature" in entry, msg=entry["path"])

        tags = header_tags(header)
        self.assertFalse("Filesignatures" in tags)
        self.assertFalse("Filesignaturelength" in tags)

        # both formats put them in the signature header now
        for fmt in FORMATS:
            pkg = self.create("new%s.rpm" % fmt, fmt, out_dir)

            entries, data = main_header(pkg)
            self.assertFalse(5090 in tag_numbers(entries))
            self.assertFalse(5091 in tag_numbers(entries))

            tags = tag_numbers(signature_header(pkg))
            self.assertTrue(274 in tags, msg="signature header is missing 274")
            self.assertTrue(275 in tags, msg="signature header is missing 275")

            qf = "[%{FILENAMES}|%{FILESIGNATURES}\n]"
            lines = query(pkg, qf).splitlines()
            line = "%s|%s" % (self.readme, FILE_SIGNATURE)
            self.assertTrue(line in lines, msg=lines)


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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
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


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyCreateFormatSixSourceNevr(TestUnpackRPM):
    """A v6 format package gets the source NEVR derived from Sourcerpm"""

    def runTest(self):
        super().runTest()

        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", six, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        sourcerpm = query(six, "%{sourcerpm}")
        self.assertTrue(sourcerpm.endswith(".src.rpm"), msg=sourcerpm)

        # the same string with the ending taken off
        self.assertEqual(query(six, "%{sourcenevr}"), sourcerpm[: -len(".src.rpm")])


class VerifyCreateFormatFourNoSourceNevr(TestUnpackRPM):
    """A v4 format package never carries the source NEVR"""

    def runTest(self):
        super().runTest()

        four = os.path.join(self.output_dir, "four.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)

        result = query(four, "%{sourcenevr}")
        self.assertIn(
            result,
            ["(none)", ""],
            msg=f"Expected '(none)' or empty string, got '{result}'",
        )


@unittest.skipUnless(
    HAVE_RPM_FORMAT_SIX, "rpm is older than 6.0.0 and cannot do format 6"
)
class VerifyFormatRoundTripKeepsSourceNevr(TestUnpackRPM):
    """Converting a package to v4 format and back keeps the source NEVR"""

    def runTest(self):
        super().runTest()

        six = os.path.join(self.output_dir, "six.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "6", "-f", six, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)
        nevr = query(six, "%{sourcenevr}")
        self.assertNotEqual(nevr, "(none)")

        # out to v4 format, which drops the tag, and back to v6 format
        tree = os.path.join(self.output_dir, "astree")
        four = os.path.join(self.output_dir, "round.rpm")
        rc, out, err = run_tarpm(
            [self.tarpm, "-c", "-F", "4", "-f", four, self.output_dir]
        )
        self.assertEqual(rc, 0, msg=err)
        self.assertEqual(query(four, "%{sourcenevr}"), "(none)")

        rc, out, err = run_tarpm([self.tarpm, "-x", "-f", four, "-O", tree])
        self.assertEqual(rc, 0, msg=err)

        back = os.path.join(self.output_dir, "back.rpm")
        rc, out, err = run_tarpm([self.tarpm, "-c", "-F", "6", "-f", back, tree])
        self.assertEqual(rc, 0, msg=err)

        self.assertEqual(query(back, "%{sourcenevr}"), nevr)
