#!/usr/bin/env python
import os
import unittest
from tempfile import NamedTemporaryFile

from flux.security import SecurityContext


def __flux_size():
    return 1


class TestSecurity(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        conf = b"""[sign]
max-ttl = 30
default-type = "none"
allowed-types = [ "none" ]
"""
        with NamedTemporaryFile() as tmpfile:
            tmpfile.file.write(conf)
            tmpfile.file.flush()
            os.fsync(tmpfile.file)
            self.context = SecurityContext(tmpfile.name)

    def test_00_sign_none(self):
        unsigned_str = u"hello world"
        signed_str = self.context.sign_wrap(unsigned_str, mech_type="none")
        unwrapped_payload, wrapping_user = self.context.sign_unwrap(signed_str)
        unwrapped_str = unwrapped_payload[:].decode("utf-8")

        self.assertEqual(unsigned_str, unwrapped_str)
        self.assertEqual(wrapping_user, os.getuid())

    def test_01_security_error(self):
        with self.assertRaisesRegexp(EnvironmentError, "sign-unwrap:.*"):
            unwrapped_payload, wrapping_user = self.context.sign_unwrap(b"foo")


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
