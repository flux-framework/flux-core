#!/usr/bin/env python

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import errno
import sys
import json
import yaml

import unittest
from glob import glob


import flux
from flux import job
from flux.job import Jobspec, JobspecV1, ffi


def __flux_size():
    return 1


def yaml_to_json(s):
    obj = yaml.load(s)
    return json.dumps(obj, separators=(",", ":"))


class TestJob(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

        self.jobspec_dir = os.path.abspath(
            os.path.join(os.environ["FLUX_SOURCE_DIR"], "t", "jobspec")
        )

        # get a valid jobspec
        basic_jobspec_fname = os.path.join(self.jobspec_dir, "valid", "basic_v1.yaml")
        with open(basic_jobspec_fname, "rb") as infile:
            basic_yaml = infile.read()
        self.basic_jobspec = yaml_to_json(basic_yaml)

    def test_00_null_submit(self):
        with self.assertRaises(EnvironmentError) as error:
            job.submit(ffi.NULL, self.basic_jobspec)
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.submit_get_id(ffi.NULL)
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.submit(self.fh, ffi.NULL)
        self.assertEqual(error.exception.errno, errno.EINVAL)

    def test_01_nonstring_submit(self):
        with self.assertRaises(TypeError):
            job.submit(self.fh, 0)

    def test_02_sync_submit(self):
        jobid = job.submit(self.fh, self.basic_jobspec)
        self.assertGreater(jobid, 0)

    def test_03_invalid_construction(self):
        for cls in [Jobspec, JobspecV1]:
            for invalid_jobspec_filepath in glob(
                os.path.join(self.jobspec_dir, "invalid", "*.yaml")
            ):
                with self.assertRaises(
                    (ValueError, TypeError, yaml.scanner.ScannerError)
                ) as cm:
                    cls.from_yaml_file(invalid_jobspec_filepath)

    def test_04_valid_construction(self):
        for jobspec_filepath in glob(os.path.join(self.jobspec_dir, "valid", "*.yaml")):
            Jobspec.from_yaml_file(jobspec_filepath)

    def test_05_valid_v1_construction(self):
        for jobspec_filepath in glob(
            os.path.join(self.jobspec_dir, "valid_v1", "*.yaml")
        ):
            JobspecV1.from_yaml_file(jobspec_filepath)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
