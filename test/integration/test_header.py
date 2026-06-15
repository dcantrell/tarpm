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
        for key in ["magic", "reserved", "index entries", "index size (bytes)", "data size (bytes)", "header size (bytes)", "tags"]:
            self.assertTrue(key in header.keys())

            if key == "magic":
                self.assertEqual(header[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(header[key], "0x0")
            elif key == "index entries":
                self.assertTrue(int(header[key]) == 51)
            elif key == "index size (bytes)":
                self.assertTrue(int(header[key]) == 816)
            elif key == "data size (bytes)":
                self.assertTrue(int(header[key]) == 917)
            elif key == "header size (bytes)":
                self.assertTrue(int(header[key]) == 1733)
            elif key == "tags":
                self.assertTrue(isinstance(header[key], list))
                self.assertTrue(len(header[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in header[key]:
                    self.assertTrue("name" in tag.keys())
                    self.assertTrue("number" in tag.keys())
                    self.assertTrue("type" in tag.keys())
                    self.assertTrue("offset" in tag.keys())
                    self.assertTrue("count" in tag.keys())

                    t = tag["name"]

                    if t == "Headerimmutable":
                        self.assertTrue(int(tag["number"]) == 63)
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(int(tag["count"]) == 16)
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Headeri18ntable":
                        self.assertTrue(int(tag["number"]) == 100)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"]) == 1)
                        self.assertTrue(tag["value"][0] == "C")
                    elif t == "Name":
                        self.assertTrue(int(tag["number"]) == 1000)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "vaporware")
                    elif t == "Version":
                        self.assertTrue(int(tag["number"]) == 1001)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "0.1")
                    elif t == "Release":
                        self.assertTrue(int(tag["number"]) == 1002)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "1")
                    elif t == "Summary":
                        self.assertTrue(int(tag["number"]) == 1004)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "Dummy summary")
                    elif t == "Description":
                        self.assertTrue(int(tag["number"]) == 1005)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "This is a dummy description.")
                    elif t == "Buildtime":
                        self.assertTrue(int(tag["number"]) == 1006)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Buildhost":
                        self.assertTrue(int(tag["number"]) == 1007)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == socket.gethostname())
                    elif t == "Size":
                        self.assertTrue(int(tag["number"]) == 1009)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "License":
                        self.assertTrue(int(tag["number"]) == 1014)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "GPL")
                    elif t == "Group":
                        self.assertTrue(int(tag["number"]) == 1016)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "Applications/Productivity")
                    elif t == "Os":
                        self.assertTrue(int(tag["number"]) == 1021)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "linux")
                    elif t == "Arch":
                        self.assertTrue(int(tag["number"]) == 1022)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == platform.uname()[4])
                    elif t == "Filesizes":
                        self.assertTrue(int(tag["number"]) == 1028)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 359)
                    elif t == "Filemodes":
                        self.assertTrue(int(tag["number"]) == 1030)
                        self.assertTrue(tag["type"] == "int16")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 4294967295)
                    elif t == "Filerdevs":
                        self.assertTrue(int(tag["number"]) == 1033)
                        self.assertTrue(tag["type"] == "int16")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "Filemtimes":
                        self.assertTrue(int(tag["number"]) == 1034)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Filedigests":
                        self.assertTrue(int(tag["number"]) == 1035)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Filelinktos":
                        self.assertTrue(int(tag["number"]) == 1036)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Fileflags":
                        self.assertTrue(int(tag["number"]) == 1037)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 32)
                    elif t == "Fileusername":
                        self.assertTrue(int(tag["number"]) == 1039)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "root")
                    elif t == "Filegroupname":
                        self.assertTrue(int(tag["number"]) == 1040)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "root")
                    elif t == "Sourcerpm":
                        self.assertTrue(int(tag["number"]) == 1044)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "vaporware-0.1-1.src.rpm")
                    elif t == "Fileverifyflags":
                        self.assertTrue(int(tag["number"]) == 1045)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "4294967295")
                    elif t == "Providename":
                        self.assertTrue(int(tag["number"]) == 1047)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["vaporware"])
                    elif t == "Requireflags":
                        self.assertTrue(int(tag["number"]) == 1048)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ["16777226", "16777226"])
                    elif t == "Requirename":
                        self.assertTrue(int(tag["number"]) == 1049)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ['rpmlib(CompressedFileNames)', 'rpmlib(FileDigests)'])
                    elif t == "Requireversion":
                        self.assertTrue(int(tag["number"]) == 1050)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ['3.0.4-1', '4.6.0-1'])
                    elif t == "Rpmversion":
                        self.assertTrue(int(tag["number"]) == 1064)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == rpm.__version__)
                    elif t == "Changelogtime":
                        self.assertTrue(int(tag["number"]) == 1080)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Changelogname":
                        self.assertTrue(int(tag["number"]) == 1081)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["John Doe <jdoe@example.com> - 0.1-1"])
                    elif t == "Changelogtext":
                        self.assertTrue(int(tag["number"]) == 1082)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["- Initial version"])
                    elif t == "Cookie":
                        self.assertTrue(int(tag["number"]) == 1094)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"].startswith(socket.gethostname()))
                    elif t == "Filedevices":
                        self.assertTrue(int(tag["number"]) == 1095)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Fileinodes":
                        self.assertTrue(int(tag["number"]) == 1096)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Filelangs":
                        self.assertTrue(int(tag["number"]) == 1097)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Provideflags":
                        self.assertTrue(int(tag["number"]) == 1112)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Provideversion":
                        self.assertTrue(int(tag["number"]) == 1113)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["0.1-1"])
                    elif t == "Sourcepackage":
                        self.assertTrue(int(tag["number"]) == 1106)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Dirindexes":
                        self.assertTrue(int(tag["number"]) == 1116)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "Basenames":
                        self.assertTrue(int(tag["number"]) == 1117)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "vaporware.spec")
                    elif t == "Dirnames":
                        self.assertTrue(int(tag["number"]) == 1118)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"][0] == "")
                    elif t == "Payloadformat":
                        self.assertTrue(int(tag["number"]) == 1124)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "cpio")
                    elif t == "Payloadcompressor":
                        self.assertTrue(int(tag["number"]) == 1125)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "gzip")
                    elif t == "Payloadflags":
                        self.assertTrue(int(tag["number"]) == 1126)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 9)
                    elif t == "Filedigestalgo":
                        self.assertTrue(int(tag["number"]) == 5011)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Encoding":
                        self.assertTrue(int(tag["number"]) == 5062)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(int(tag["number"]) == 5092)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        self.assertTrue(int(tag["number"]) == 5093)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Payloadsha256alt":
                        self.assertTrue(int(tag["number"]) == 5097)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Spec":
                        self.assertTrue(int(tag["number"]) == 5099)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "\n\n\nSummary: Dummy summary\nName: vaporware\nVersion: 0.1\nRelease: 1\nLicense: GPL\nGroup: Applications/Productivity\n\n\n%description\nThis is a dummy description.\n\n\n%prep\n\n%build\n\n%install\n\n%files\n\n%changelog\n* Sun Jul 22 2018 John Doe <jdoe@example.com> - 0.1-1\n- Initial version\n\n")
                    else:
                        m = "Unknown tag found in header: %s\n%s\n" % (t, str(tag))
                        self.fail(msg=m)

class VerifyHeaderExtractRPM(TestUnpackRPM):
    def runTest(self):
        super().runTest()

        f = open(self.header)
        header = json.load(f)
        f.close()

        # Check main header fields
        for key in ["magic", "reserved", "index entries", "index size (bytes)", "data size (bytes)", "header size (bytes)", "tags"]:
            self.assertTrue(key in header.keys())

            if key == "magic":
                self.assertEqual(header[key], "0x8EADE801")
            elif key == "reserved":
                self.assertEqual(header[key], "0x0")
            elif key == "index entries":
                self.assertTrue(int(header[key]) == 37)
            elif key == "index size (bytes)":
                self.assertTrue(int(header[key]) == 592)
            elif key == "data size (bytes)":
                self.assertTrue(int(header[key]) == 1165)
            elif key == "header size (bytes)":
                self.assertTrue(int(header[key]) == 1757)
            elif key == "tags":
                self.assertTrue(isinstance(header[key], list))
                self.assertTrue(len(header[key]) > 0)

                # Verify each tag entry has expected fields
                for tag in header[key]:
                    self.assertTrue("name" in tag.keys())
                    self.assertTrue("number" in tag.keys())
                    self.assertTrue("type" in tag.keys())
                    self.assertTrue("offset" in tag.keys())
                    self.assertTrue("count" in tag.keys())

                    t = tag["name"]

                    if t == "Headerimmutable":
                        self.assertTrue(int(tag["number"]) == 63)
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(int(tag["count"]) == 16)
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Headeri18ntable":
                        self.assertTrue(int(tag["number"]) == 100)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"]) == 1)
                        self.assertTrue(tag["value"][0] == "C")
                    elif t == "Name":
                        self.assertTrue(int(tag["number"]) == 1000)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "vaporware")
                    elif t == "Version":
                        self.assertTrue(int(tag["number"]) == 1001)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "0.1")
                    elif t == "Release":
                        self.assertTrue(int(tag["number"]) == 1002)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "1")
                    elif t == "Summary":
                        self.assertTrue(int(tag["number"]) == 1004)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "Dummy summary")
                    elif t == "Description":
                        self.assertTrue(int(tag["number"]) == 1005)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "This is a dummy description.")
                    elif t == "Buildtime":
                        self.assertTrue(int(tag["number"]) == 1006)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Buildhost":
                        self.assertTrue(int(tag["number"]) == 1007)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == socket.gethostname())
                    elif t == "Size":
                        self.assertTrue(int(tag["number"]) == 1009)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 0)
                    elif t == "License":
                        self.assertTrue(int(tag["number"]) == 1014)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "GPL")
                    elif t == "Group":
                        self.assertTrue(int(tag["number"]) == 1016)
                        self.assertTrue(tag["type"] == "i18n string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "Applications/Productivity")
                    elif t == "Os":
                        self.assertTrue(int(tag["number"]) == 1021)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "linux")
                    elif t == "Arch":
                        self.assertTrue(int(tag["number"]) == 1022)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == platform.uname()[4])
                    elif t == "Sourcerpm":
                        self.assertTrue(int(tag["number"]) == 1044)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "vaporware-0.1-1.src.rpm")
                    elif t == "Providename":
                        self.assertTrue(int(tag["number"]) == 1047)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ["vaporware", "vaporware(%s)" % platform.uname()[4].replace('_', '-')])
                    elif t == "Requireflags":
                        self.assertTrue(int(tag["number"]) == 1048)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 4)
                        self.assertTrue(tag["value"] == ["16777226", "16777226", "16777226", "16777226"])
                    elif t == "Requirename":
                        self.assertTrue(int(tag["number"]) == 1049)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 4)
                        self.assertTrue(tag["value"] == ['rpmlib(CompressedFileNames)', 'rpmlib(FileDigests)', 'rpmlib(PayloadFilesHavePrefix)', 'rpmlib(PayloadIsZstd)'])
                    elif t == "Requireversion":
                        self.assertTrue(int(tag["number"]) == 1050)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 4)
                        self.assertTrue(tag["value"] == ['3.0.4-1', '4.6.0-1', '4.0-1', '5.4.18-1'])
                    elif t == "Rpmversion":
                        self.assertTrue(int(tag["number"]) == 1064)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == rpm.__version__)
                    elif t == "Changelogtime":
                        self.assertTrue(int(tag["number"]) == 1080)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) > 0)
                    elif t == "Changelogname":
                        self.assertTrue(int(tag["number"]) == 1081)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["John Doe <jdoe@example.com> - 0.1-1"])
                    elif t == "Changelogtext":
                        self.assertTrue(int(tag["number"]) == 1082)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == ["- Initial version"])
                    elif t == "Cookie":
                        self.assertTrue(int(tag["number"]) == 1094)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"].startswith(socket.gethostname()))
                    elif t == "Provideflags":
                        self.assertTrue(int(tag["number"]) == 1112)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ['8', '8'])
                    elif t == "Provideversion":
                        self.assertTrue(int(tag["number"]) == 1113)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 2)
                        self.assertTrue(tag["value"] == ["0.1-1", "0.1-1"])
                    elif t == "Optflags":
                        self.assertTrue(int(tag["number"]) == 1122)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"]) > 0)
                    elif t == "Payloadformat":
                        self.assertTrue(int(tag["number"]) == 1124)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "cpio")
                    elif t == "Payloadcompressor":
                        self.assertTrue(int(tag["number"]) == 1125)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "zstd")
                    elif t == "Payloadflags":
                        self.assertTrue(int(tag["number"]) == 1126)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 19)
                    elif t == "Platform":
                        self.assertTrue(int(tag["number"]) == 1132)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"].startswith(platform.uname()[4]) and (tag["value"].find(platform.uname()[0].lower()) != -1))
                    elif t == "Sourcesigmd5":
                        self.assertTrue(int(tag["number"]) == 1146)
                        self.assertTrue(tag["type"] == "binary blob")
                        self.assertTrue(int(tag["count"]) == 16)
                        self.assertTrue(len(tag["value"]) == 25)
                    elif t == "Filedigestalgo":
                        self.assertTrue(int(tag["number"]) == 5011)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Encoding":
                        self.assertTrue(int(tag["number"]) == 5062)
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(int(tag["number"]) == 5092)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        self.assertTrue(int(tag["number"]) == 5093)
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(int(tag["value"]) == 8)
                    elif t == "Payloadsha256alt":
                        self.assertTrue(int(tag["number"]) == 5097)
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(int(tag["count"]) == 1)
                        self.assertTrue(len(tag["value"][0]) == 64)
                    else:
                        m = "Unknown tag found in header: %s\n%s\n" % (t, str(tag))
                        self.fail(msg=m)
