###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import os
import re

parser = argparse.ArgumentParser()

parser.add_argument("header", help="C header file to parse", type=str)
parser.add_argument("--include_header", help="Include base path", type=str, default="")
parser.add_argument(
    "--additional_headers", help="Additional headers to parse", nargs="*", default=[]
)
parser.add_argument("--output", help="Output path", default=".")
parser.add_argument("--path", help="Include base path", default=".")
parser.add_argument(
    "--search", help="append to header search path", action="append", default=[]
)
parser.add_argument(
    "--ignore_header",
    help="Pattern to ignore when searching header files",
    default=[],
    action="append",
)

args = parser.parse_args()

absolute_head = os.path.abspath(os.path.join(args.path, args.header))

# Prepend 'path' option to search list:
args.search.insert(0, args.path)
args.search = [os.path.abspath(f) for f in args.search]


def find_first(path, name, extra=None):
    for dirname in path:
        filename = os.path.join(dirname, name)
        if os.path.isfile(filename):
            return filename
    if extra is not None:
        filename = os.path.join(extra, name)
        if os.path.isfile(filename):
            return filename
    raise IOError(name)


mega_header = ""


def process_header(f, including_path="."):
    global mega_header
    if not os.path.isfile(f):
        f = os.path.join(including_path, f)
    f = os.path.abspath(f)
    if f not in checked_heads:
        for p in args.ignore_header:
            if re.search(p, f):
                checked_heads[f] = 1
                return
        with open(f, "r") as header:
            for l in header.readlines():
                m = re.search('#include\s*"([^"]*)"', l)
                if m:
                    nf = find_first(args.search, m.group(1), including_path)
                    process_header(nf, os.path.dirname(os.path.abspath(nf)))
                if not re.match("#\s*include", l):
                    mega_header += l
        checked_heads[f] = 1


orig_wd = os.getcwd()
os.chdir(args.path)

checked_heads = {}

process_header(absolute_head)

for header in args.additional_headers:
    process_header(header)

include_head = args.header
if args.include_header != "":
    include_head = args.include_header

os.chdir(orig_wd)
with open(args.output, "w") as clean_header:
    clean_header.write(mega_header)
