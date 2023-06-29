###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from _flux._core import ffi
from flux.job._wrapper import _RAW as RAW


def id_parse(jobid_str):
    """
    returns: An integer jobid
    :rtype int
    """
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.id_parse(jobid_str, jobid)
    return int(jobid[0])


def id_encode(jobid, encoding="f58"):
    """
    returns: Jobid encoded in encoding
    :rtype str
    """
    buflen = 128
    buf = ffi.new("char[]", buflen)
    RAW.id_encode(int(jobid), encoding, buf, buflen)
    return ffi.string(buf, buflen).decode("utf-8")


class JobID(int):
    """Class used to represent a Flux JOBID

    JobID is a subclass of `int`, so may be used in place of integer.
    However, a JobID may be created from any valid RFC 19 FLUID
    encoding, including:

     - decimal integer (no prefix)
     - hexadecimal integer (prefix 0x)
     - dotted hex (dothex) (xxxx.xxxx.xxxx.xxxx)
     - kvs dir (dotted hex with `job.` prefix)
     - RFC19 F58: (Base58 encoding with prefix `ƒ` or `f`)
     - basemoji (emoji encoding)

    A JobID object also has properties for encoding a JOBID into each
    of the above representations, e.g. jobid.f85, jobid.words, jobid.dothex...

    """

    def __new__(cls, value):
        if isinstance(value, JobID):
            return cls(value.orig_str)
        if isinstance(value, int):
            jobid = value
        else:
            try:
                jobid = id_parse(value)
            except OSError:
                raise ValueError(f"{value} is not a valid Flux jobid")
        self = super(cls, cls).__new__(cls, jobid)
        self.orig_str = str(value)
        return self

    def encode(self, encoding="dec"):
        """Encode a JobID to alternate supported format"""
        return id_encode(self, encoding)

    @property
    def dec(self):
        """Return decimal integer representation of a JobID"""
        return self.encode()

    @property
    def f58(self):
        """Return RFC19 F58 representation of a JobID"""
        return self.encode("f58")

    @property
    def f58plain(self):
        """Return RFC19 F58 representation of a JobID with ASCII prefix"""
        return self.encode("f58plain")

    @property
    def hex(self):
        """Return 0x-prefixed hexadecimal representation of a JobID"""
        return self.encode("hex")

    @property
    def dothex(self):
        """Return dotted hexadecimal representation of a JobID"""
        return self.encode("dothex")

    @property
    def words(self):
        """Return words (mnemonic) representation of a JobID"""
        return self.encode("words")

    @property
    def emoji(self):
        """Return emoji representation of a JobID"""
        return self.encode("emoji")

    @property
    def kvs(self):
        """Return KVS directory path of a JobID"""
        return self.encode("kvs")

    @property
    def orig(self):
        """Return the original string used to create the JobID"""
        return self.orig_str

    def __str__(self):
        return self.encode("f58")

    def __repr__(self):
        return f"JobID({self.dec})"
