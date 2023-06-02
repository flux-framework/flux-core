#!/usr/bin/env python3
###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import base64
import random
import stat
import tempfile
import unittest

import subflux  # noqa: F401 -  To set up PYTHONPATH
from flux.util import Fileref
from pycotap import TAPTestRunner


class TestFileref(unittest.TestCase):
    def test_dict(self):
        data = {"foo": True}
        mode = stat.S_IFREG | 0o600
        result = Fileref(data)
        self.assertDictEqual(result, {"mode": mode, "data": data})

        mode = stat.S_IFREG | 0o700
        result = Fileref(data, perms=0o700)
        self.assertDictEqual(result, {"mode": mode, "data": data})

    def test_raw(self):
        data = "this is a testfile\n"
        mode = stat.S_IFREG | 0o600
        encoding = "utf-8"
        result = Fileref(data, encoding=encoding)
        expected = {"mode": mode, "encoding": encoding, "data": data}
        self.assertDictEqual(result, expected)

        with self.assertRaises(ValueError):
            Fileref(data, encoding="bad")

    def test_file(self):

        # utf-8
        data = "This is a text file\n"
        mode = stat.S_IFREG | 0o600
        with tempfile.NamedTemporaryFile(mode="w") as fp:
            fp.write(data)
            fp.flush()
            path = fp.name
            result = Fileref(path)
            expected = {
                "mode": mode,
                "encoding": "utf-8",
                "data": data,
                "size": len(data),
            }
            self.assertDictEqual(result, expected)

        # binary
        data = bytearray(map(random.getrandbits, (8,) * 32))
        mode = stat.S_IFREG | 0o600
        with tempfile.NamedTemporaryFile() as fp:
            fp.write(data)
            fp.flush()
            path = fp.name
            result = Fileref(path)
            expected = {
                "mode": mode,
                "encoding": "base64",
                "data": base64.b64encode(data).decode("utf-8"),
                "size": 32,
            }
            self.assertDictEqual(result, expected)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
