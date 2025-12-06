#!/usr/bin/env python3

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import locale
import os
import subprocess
import unittest

import flux
import yaml
from flux import job


def __flux_size():
    return 1


def read_yaml(path):
    with open(path, "r") as fd:
        basic_yaml = fd.read()
    return yaml_to_json(basic_yaml)


def yaml_to_json(s):
    obj = yaml.safe_load(s)
    return json.dumps(obj, separators=(",", ":"))


def determine_ascii():
    use_ascii = False
    build_opts = subprocess.check_output(["flux", "version"]).decode()
    if locale.getlocale()[1] != "UTF-8" or "ascii-only" in build_opts:
        use_ascii = True
    return use_ascii


class TestJobValidate(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        os.unsetenv("FLUX_F58_FORCE_ASCII")
        self.fh = flux.Flux()
        self.use_ascii = determine_ascii()
        self.jobspec_dir = os.path.abspath(
            os.path.join(os.environ["FLUX_SOURCE_DIR"], "t", "jobspec")
        )
        jobspec_path = os.path.join(self.jobspec_dir, "valid", "basic_v1.yaml")
        self.basic_jobspec = read_yaml(jobspec_path)

    def test_00_valid_submit(self):
        job.submit(self.fh, self.basic_jobspec)

    def test_01_non_integer_submit(self):
        js = json.loads(self.basic_jobspec)

        # Change to string
        js["resources"][0]["count"] = "1"

        # resource count must be an integer for type 'slot' (got 'str')
        with self.assertRaises(EnvironmentError):
            job.submit(self.fh, json.dumps(js))

        # Negative
        js["resources"][0]["count"] = -1

        # node, slot, or core count must be > 0
        with self.assertRaises(EnvironmentError):
            job.submit(self.fh, json.dumps(js))

        # 0 is (still) not OK
        js["resources"][0]["count"] = 0
        with self.assertRaises(EnvironmentError):
            job.submit(self.fh, json.dumps(js))

        # float values not OK
        js["resources"][0]["count"] = 1.0
        with self.assertRaises(EnvironmentError):
            job.submit(self.fh, json.dumps(js))


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
