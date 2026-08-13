#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import json
import socket
import os
import platform
import rpm
from baseclass import TestUnpackSRPM, TestUnpackRPM


class VerifyHeaderExtractSRPM(TestUnpackSRPM):
    def runTest(self):
        super().runTest()

        f = open(self.header)
        header = json.load(f)
        f.close()

        # Check main header fields
        for key in [
            "magic",
            "reserved",
            "tags",
        ]:
            self.assertTrue(key in header.keys())

            if key == "magic":
                self.assertEqual(header[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(header[key], "0000")
            elif key == "tags":
                self.assertTrue(isinstance(header[key], list))
                self.assertTrue(len(header[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in header[key]:
                    self.assertTrue("tag" in tag.keys())
                    self.assertTrue("type" in tag.keys())

                    t = tag["tag"]

                    if t == "Headerimmutable":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                        self.assertTrue("trailer" in tag.keys())
                    elif t == "Headeri18ntable":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"]) == 1)
                        self.assertTrue(tag["value"][0] == "C")
                    elif t == "Name":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "vaporware")
                    elif t == "Version":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "0.1")
                    elif t == "Release":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "1")
                    elif t == "Summary":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["value"] == "Dummy summary")
                    elif t == "Description":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["file"] == "description.txt")
                    elif t == "Buildtime":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Buildhost":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == socket.gethostname())
                    elif t == "Size":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "License":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "GPL")
                    elif t == "Group":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["value"] == "Applications/Productivity")
                    elif t == "Os":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "linux")
                    elif t == "Arch":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == platform.uname()[4])
                    elif t == "Filesizes":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 359)
                    elif t == "Filemodes":
                        self.assertTrue(tag["type"] == "int16")
                        self.assertTrue(int(tag["value"]) == 33188)
                    elif t == "Filerdevs":
                        self.assertTrue(tag["type"] == "int16")
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "Filemtimes":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Filedigests":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Filelinktos":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Fileflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 32)
                    elif t == "Fileusername":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "root")
                    elif t == "Filegroupname":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "root")
                    elif t == "Sourcerpm":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "vaporware-0.1-1.src.rpm")
                    elif t == "Fileverifyflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] == -1)
                    elif t == "Providename":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["vaporware"])
                    elif t == "Requireflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] == [16777226, 16777226])
                    elif t == "Requirename":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(
                            tag["value"]
                            == ["rpmlib(CompressedFileNames)", "rpmlib(FileDigests)"]
                        )
                    elif t == "Requireversion":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["3.0.4-1", "4.6.0-1"])
                    elif t == "Rpmversion":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == rpm.__version__)
                    elif t == "Changelogtime":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Changelogname":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(
                            tag["value"] == ["John Doe <jdoe@example.com> - 0.1-1"]
                        )
                    elif t == "Changelogtext":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["- Initial version"])
                    elif t == "Cookie":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"].startswith(socket.gethostname()))
                    elif t == "Filedevices":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Fileinodes":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Filelangs":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Provideflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Provideversion":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["0.1-1"])
                    elif t == "Sourcepackage":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Dirindexes":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "Basenames":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "vaporware.spec")
                    elif t == "Dirnames":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Payloadformat":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "cpio")
                    elif t == "Payloadcompressor":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "gzip")
                    elif t == "Payloadflags":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["value"]) == 9)
                    elif t == "Filedigestalgo":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Encoding":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Payloadsha256alt":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Spec":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue("file" in tag.keys())
                        self.assertTrue(tag["file"] == "spec.txt")

                        spec_file = os.path.join(
                            os.path.dirname(self.header), tag["file"]
                        )
                        self.assertTrue(os.path.exists(spec_file))
                        f = open(spec_file, "r")
                        spec_content = f.read()
                        f.close()
                        self.assertTrue(
                            spec_content
                            == "\n\n\nSummary: Dummy summary\nName: vaporware\nVersion: 0.1\nRelease: 1\nLicense: GPL\nGroup: Applications/Productivity\n\n\n%description\nThis is a dummy description.\n\n\n%prep\n\n%build\n\n%install\n\n%files\n\n%changelog\n* Sun Jul 22 2018 John Doe <jdoe@example.com> - 0.1-1\n- Initial version\n\n"
                        )
                    else:
                        m = "Unknown tag found in header: %s\n%s\n" % (t, str(tag))
                        self.fail(msg=m)


class VerifyHeaderExtractRPM(TestUnpackRPM):
    def runTest(self):
        is_zstd = False

        super().runTest()

        f = open(self.header)
        header = json.load(f)
        f.close()

        # Check main header fields
        for key in [
            "magic",
            "reserved",
            "tags",
        ]:
            self.assertTrue(key in header.keys())

            if key == "magic":
                self.assertEqual(header[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(header[key], "0000")
            elif key == "tags":
                self.assertTrue(isinstance(header[key], list))
                self.assertTrue(len(header[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in header[key]:
                    self.assertTrue("tag" in tag.keys())
                    self.assertTrue("type" in tag.keys())

                    t = tag["tag"]

                    if t == "Headerimmutable":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                        self.assertTrue("trailer" in tag.keys())
                    elif t == "Headeri18ntable":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"]) == 1)
                        self.assertTrue(tag["value"][0] == "C")
                    elif t == "Name":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "vaporware")
                    elif t == "Version":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "0.1")
                    elif t == "Release":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "1")
                    elif t == "Summary":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["value"] == "Dummy summary")
                    elif t == "Description":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["file"] == "description.txt")
                    elif t == "Buildtime":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Buildhost":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == socket.gethostname())
                    elif t == "Size":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "License":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "GPL")
                    elif t == "Group":
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(tag["value"] == "Applications/Productivity")
                    elif t == "Os":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "linux")
                    elif t == "Arch":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == platform.uname()[4])
                    elif t == "Sourcerpm":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "vaporware-0.1-1.src.rpm")
                    elif t == "Providename":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(
                            tag["value"]
                            == [
                                "vaporware",
                                "vaporware(%s)" % platform.uname()[4].replace("_", "-"),
                            ]
                        )
                    elif t == "Requireflags":
                        self.assertTrue(tag["type"] == "int32")

                        for entry in tag["value"]:
                            self.assertTrue(entry == 16777226)
                    elif t == "Requirename":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue("rpmlib(CompressedFileNames)" in tag["value"])
                        self.assertTrue("rpmlib(FileDigests)" in tag["value"])
                        self.assertTrue(
                            "rpmlib(PayloadFilesHavePrefix)" in tag["value"]
                        )

                        # the payload can be compressed different ways or not
                        if "rpmlib(PayloadIsZstd)" in tag["value"]:
                            is_zstd = True
                            self.assertTrue("rpmlib(PayloadIsZstd)" in tag["value"])
                    elif t == "Requireversion":
                        self.assertTrue(tag["type"] == "string array")

                        self.assertTrue("3.0.4-1" in tag["value"])
                        self.assertTrue("4.6.0-1" in tag["value"])
                        self.assertTrue("4.0-1" in tag["value"])

                        if is_zstd:
                            self.assertTrue("5.4.18-1" in tag["value"])
                    elif t == "Rpmversion":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == rpm.__version__)
                    elif t == "Changelogtime":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Changelogname":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(
                            tag["value"] == ["John Doe <jdoe@example.com> - 0.1-1"]
                        )
                    elif t == "Changelogtext":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["- Initial version"])
                    elif t == "Cookie":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"].startswith(socket.gethostname()))
                    elif t == "Provideflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] == [8, 8])
                    elif t == "Provideversion":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"] == ["0.1-1", "0.1-1"])
                    elif t == "Optflags":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) > 0)
                    elif t == "Payloadformat":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "cpio")
                    elif t == "Payloadcompressor":
                        self.assertTrue(tag["type"] == "string")

                        if is_zstd:
                            self.assertTrue(tag["value"] == "zstd")
                        else:
                            self.assertTrue(tag["value"] == "gzip")
                    elif t == "Payloadflags":
                        self.assertTrue(tag["type"] == "string")

                        if is_zstd:
                            self.assertTrue(
                                int(tag["value"]) >= 1 or int(tag["value"]) <= 19
                            )
                        else:
                            self.assertTrue(
                                int(tag["value"]) >= 1 or int(tag["value"]) <= 9
                            )
                    elif t == "Platform":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(
                            tag["value"].startswith(platform.uname()[4])
                            and (tag["value"].find(platform.uname()[0].lower()) != -1)
                        )
                    elif t == "Sourcesigmd5":
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Filedigestalgo":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Encoding":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Payloadsha256alt":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    else:
                        m = "Unknown tag found in header: %s\n%s\n" % (t, str(tag))
                        self.fail(msg=m)
