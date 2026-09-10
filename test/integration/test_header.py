#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: GPL-3.0-or-later
#

import calendar
import json
import socket
import os
import platform
import rpm
import time
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
                        # recorded as an ISO 8601 timestamp in UTC
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(
                            calendar.timegm(
                                time.strptime(tag["value"], "%Y-%m-%dT%H:%M:%SZ")
                            )
                            > 0
                        )
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
                    elif t == "Sourcerpm":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "vaporware-0.1-1.src.rpm")
                    elif t == "Fileverifyflags":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(tag["value"] == -1)
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
                    elif t == "Fileinodes":
                        self.assertTrue(tag["type"] == "int32")
                        self.assertTrue(int(tag["value"]) == 1)
                    elif t == "Filelangs":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(tag["value"][0] == "")
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
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "sha256")
                    elif t == "Encoding":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        # recorded by name, the same as Filedigestalgo
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "sha256")
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

        # Check changelog array if present
        if "changelog" in header.keys():
            self.assertTrue(isinstance(header["changelog"], list))
            self.assertTrue(len(header["changelog"]) > 0)

            for entry in header["changelog"]:
                # Each changelog entry must have these fields
                self.assertTrue("timestamp" in entry.keys())
                self.assertTrue("name" in entry.keys())
                self.assertTrue("text" in entry.keys())

                # Verify types
                self.assertTrue(isinstance(entry["timestamp"], str))
                self.assertTrue(isinstance(entry["name"], str))
                self.assertTrue(isinstance(entry["text"], list))

                # Verify timestamp is not empty
                self.assertTrue(len(entry["timestamp"]) > 0)

                # Verify name format (should contain email and version)
                self.assertTrue("@" in entry["name"])
                self.assertTrue("-" in entry["name"])

                # Verify text is an array of strings
                self.assertTrue(len(entry["text"]) > 0)
                for line in entry["text"]:
                    self.assertTrue(isinstance(line, str))

        # Check dependencies object if present
        if "dependencies" in header.keys():
            self.assertTrue(isinstance(header["dependencies"], dict))

            # Valid dependency types
            valid_dep_types = [
                "provides", "requires", "conflicts", "obsoletes",
                "recommends", "suggests", "supplements", "enhances"
            ]

            for dep_type, deps_array in header["dependencies"].items():
                # Verify dependency type is valid
                self.assertTrue(dep_type in valid_dep_types)

                # Verify it's an array
                self.assertTrue(isinstance(deps_array, list))
                self.assertTrue(len(deps_array) > 0)

                for dep in deps_array:
                    # Each dependency must be a dict with at least a name
                    self.assertTrue(isinstance(dep, dict))
                    self.assertTrue("name" in dep.keys())
                    self.assertTrue(isinstance(dep["name"], str))
                    self.assertTrue(len(dep["name"]) > 0)

                    # Optional: comparison (if present, must be a string)
                    if "comparison" in dep.keys():
                        self.assertTrue(isinstance(dep["comparison"], str))
                        self.assertTrue(dep["comparison"] in ["<", "<=", ">", ">=", "="])

                    # Optional: version (if present, must be a string)
                    if "version" in dep.keys():
                        self.assertTrue(isinstance(dep["version"], str))

                    # Optional: sense_flags (if present, must be array of strings)
                    if "sense_flags" in dep.keys():
                        self.assertTrue(isinstance(dep["sense_flags"], list))
                        for flag in dep["sense_flags"]:
                            self.assertTrue(isinstance(flag, str))


class VerifyHeaderExtractRPM(TestUnpackRPM):
    def runTest(self):
        is_zstd = False

        super().runTest()

        f = open(self.header)
        header = json.load(f)
        f.close()

        # Check for dependencies object and set is_zstd flag
        if "dependencies" in header and "requires" in header["dependencies"]:
            for req in header["dependencies"]["requires"]:
                if req.get("name") == "rpmlib(PayloadIsZstd)":
                    is_zstd = True
                    break

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
                        # recorded as an ISO 8601 timestamp in UTC
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(
                            calendar.timegm(
                                time.strptime(tag["value"], "%Y-%m-%dT%H:%M:%SZ")
                            )
                            > 0
                        )
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
                    elif t == "Optflags":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(len(tag["value"]) > 0)
                    elif t == "Payloadformat":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "cpio")
                    elif t == "Payloadcompressor":
                        self.assertTrue(tag["type"] == "string")
                        # Check against is_zstd flag which was set based on dependencies
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
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "sha256")
                    elif t == "Encoding":
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "utf-8")
                    elif t == "Payloadsha256":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    elif t == "Payloadsha256algo":
                        # recorded by name, the same as Filedigestalgo
                        self.assertTrue(tag["type"] == "string")
                        self.assertTrue(tag["value"] == "sha256")
                    elif t == "Payloadsha256alt":
                        self.assertTrue(tag["type"] == "string array")
                        self.assertTrue(len(tag["value"][0]) == 64)
                    else:
                        m = "Unknown tag found in header: %s\n%s\n" % (t, str(tag))
                        self.fail(msg=m)

        # Check changelog array if present
        if "changelog" in header.keys():
            self.assertTrue(isinstance(header["changelog"], list))
            self.assertTrue(len(header["changelog"]) > 0)

            for entry in header["changelog"]:
                # Each changelog entry must have these fields
                self.assertTrue("timestamp" in entry.keys())
                self.assertTrue("name" in entry.keys())
                self.assertTrue("text" in entry.keys())

                # Verify types
                self.assertTrue(isinstance(entry["timestamp"], str))
                self.assertTrue(isinstance(entry["name"], str))
                self.assertTrue(isinstance(entry["text"], list))

                # Verify timestamp is not empty
                self.assertTrue(len(entry["timestamp"]) > 0)

                # Verify name format (should contain email and version)
                self.assertTrue("@" in entry["name"])
                self.assertTrue("-" in entry["name"])

                # Verify text is an array of strings
                self.assertTrue(len(entry["text"]) > 0)
                for line in entry["text"]:
                    self.assertTrue(isinstance(line, str))

        # Check dependencies object if present
        if "dependencies" in header.keys():
            self.assertTrue(isinstance(header["dependencies"], dict))

            # Valid dependency types
            valid_dep_types = [
                "provides", "requires", "conflicts", "obsoletes",
                "recommends", "suggests", "supplements", "enhances"
            ]

            for dep_type, deps_array in header["dependencies"].items():
                # Verify dependency type is valid
                self.assertTrue(dep_type in valid_dep_types)

                # Verify it's an array
                self.assertTrue(isinstance(deps_array, list))
                self.assertTrue(len(deps_array) > 0)

                for dep in deps_array:
                    # Each dependency must be a dict with at least a name
                    self.assertTrue(isinstance(dep, dict))
                    self.assertTrue("name" in dep.keys())
                    self.assertTrue(isinstance(dep["name"], str))
                    self.assertTrue(len(dep["name"]) > 0)

                    # Optional: comparison (if present, must be a string)
                    if "comparison" in dep.keys():
                        self.assertTrue(isinstance(dep["comparison"], str))
                        self.assertTrue(dep["comparison"] in ["<", "<=", ">", ">=", "="])

                    # Optional: version (if present, must be a string)
                    if "version" in dep.keys():
                        self.assertTrue(isinstance(dep["version"], str))

                    # Optional: sense_flags (if present, must be array of strings)
                    if "sense_flags" in dep.keys():
                        self.assertTrue(isinstance(dep["sense_flags"], list))
                        for flag in dep["sense_flags"]:
                            self.assertTrue(isinstance(flag, str))
