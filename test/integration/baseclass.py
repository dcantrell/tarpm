#
# Copyright The tarpm Project Authors
# SPDX-License-Identifier: Apache-2.0
#

import os
import unittest
import rpmfluff

# NVRs to use for the fake test package
NAME = "vaporware"
VER = "0.1"
REL = "1"

VENDOR = "tarpm Test Vendor Ltd."

# Run the test suite with KEEP=y (or set to anything) in the
# environment to instruct the test suite to keep intermediate files
# and results.
if "KEEP" in os.environ:
    KEEP_RESULTS = False
else:
    KEEP_RESULTS = True


# Exceptions used by the test suite
class MissingTarpm(Exception):
    pass


# Base test case class that ensures we have 'tarpm' as an executable
# command.
class RequiresTarpm(unittest.TestCase):
    def setUp(self):
        super().setUp()

        # make sure we have the program
        self.tarpm = os.getenv("TARPM")

        if (
            self.tarpm is None
            or not os.path.isfile(self.tarpm)
            or not os.access(self.tarpm, os.X_OK)
        ):
            raise MissingTarpm


# Base test case class that tests a source RPM
class TestSRPM(RequiresTarpm):
    def setUp(self):
        super().setUp()
        self.srpm = rpmfluff.SimpleSrpmBuild(NAME, VER, REL)

    def runTest(self):
        self.srpm.do_make()

    def tearDown(self):
        super().tearDown()

        if not KEEP_RESULTS:
            self.srpm.clean()


# Base test case class that tests a binary RPM
class TestRPM(RequiresTarpm):
    def setUp(self):
        super().setUp()
        self.rpm = rpmfluff.SimpleRpmBuild(NAME, VER, REL)

    def runTest(self):
        self.rpm.do_make()

    def tearDown(self):
        super().tearDown()

        if not KEEP_RESULTS:
            self.rpm.clean()
