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
import unittest
import datetime
import itertools
from glob import glob

import yaml
import six

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

    def test_06_iter(self):
        jobspec_fname = os.path.join(self.jobspec_dir, "valid", "use_case_2.4.yaml")
        jobspec = Jobspec.from_yaml_file(jobspec_fname)
        self.assertEqual(len(list(jobspec)), 7)
        self.assertEqual(len(list([x for x in jobspec if x["type"] == "core"])), 2)
        self.assertEqual(len(list([x for x in jobspec if x["type"] == "slot"])), 2)

    def test_07_count(self):
        jobspec_fname = os.path.join(self.jobspec_dir, "valid", "use_case_2.4.yaml")
        jobspec = Jobspec.from_yaml_file(jobspec_fname)
        count_dict = jobspec.resource_counts()
        self.assertEqual(count_dict["node"], 1)
        self.assertEqual(count_dict["slot"], 11)
        self.assertEqual(count_dict["core"], 16)
        self.assertEqual(count_dict["memory"], 64)

    def test_08_jobspec_submit(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        jobid = job.submit(self.fh, jobspec)
        self.assertGreater(jobid, 0)

    def test_09_duration_timedelta(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        duration = 100.5
        delta = datetime.timedelta(seconds=duration)
        for x in [duration, delta, "{}s".format(duration)]:
            jobspec.duration = delta
            self.assertEqual(jobspec.duration, duration)

    def test_10_cwd_pathlib(self):
        if six.PY2:
            return
        import pathlib

        jobspec_path = pathlib.PosixPath(self.jobspec_dir) / "valid" / "basic_v1.yaml"
        jobspec = Jobspec.from_yaml_file(jobspec_path)
        cwd = pathlib.PosixPath("/tmp")
        jobspec.cwd = cwd
        self.assertEqual(jobspec.cwd, os.fspath(cwd))

    def test_11_environment(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        new_env = {"HOME": "foo", "foo": "bar"}
        jobspec.environment = new_env
        self.assertEqual(jobspec.environment, new_env)

    def test_12_convert_id(self):
        variants = {
            "dec": 74859937792,
            "hex": "0000.0011.6e00.0000",
            "words": "algebra-arizona-susan--album-academy-academy",
        }
        variants["kvs"] = 'job.{}'.format(variants['hex'])

        for (src_type, src_value), (dest_type, dest_value) in \
            itertools.product(six.iteritems(variants), repeat=2):

            converted_value = job.convert_id(src_value, src_type, dest_type)
            self.assertEqual(
                converted_value,
                dest_value,
                msg="Failed to convert id of type {} into an id of type {} ({} != {})".format(
                    src_type,
                    dest_type,
                    converted_value,
                    dest_value,
                )
            )

    def test_13_convert_id_unicode(self):
        converted_value = job.convert_id(
            u"algebra-arizona-susan--album-academy-academy",
            "words",
            "hex")
        self.assertEqual(converted_value, u"0000.0011.6e00.0000")
        self.assertEqual(converted_value, b"0000.0011.6e00.0000")

    def test_05_convert_id_errors(self):
        with self.assertRaises(TypeError) as error:
            job.convert_id(5.0)

        with self.assertRaises(EnvironmentError) as error:
            job.convert_id(74859937792, src="foo")
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.convert_id(74859937792, dst="foo")
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.convert_id("foo.bar", src="kvs")
        self.assertEqual(error.exception.errno, errno.EINVAL)

if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
