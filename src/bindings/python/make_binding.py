###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from __future__ import print_function
import os
import re

import argparse

parser = argparse.ArgumentParser()

parser.add_argument("header", help="C header file to parse", type=str)
parser.add_argument("--include_header", help="Include base path", type=str, default="")
parser.add_argument(
    "--include_ffi", help="FFI module for inclusion", action="append", default=[]
)
parser.add_argument("--package", help="Package prefix for module import", default=None)
parser.add_argument("--path", help="Include base path", default=".")
parser.add_argument(
    "--search", help="append to header search path", action="append", default=[]
)
parser.add_argument(
    "--modname", help="Name for the module to be generated", default="_flux"
)
parser.add_argument(
    "--library", help="Library to include in the build", default="flux-core"
)
parser.add_argument(
    "--add_long_sub",
    help="Regex filter to apply whole-file of the form <match>|||<replacement>",
    action="append",
    default=[],
)
parser.add_argument(
    "--add_sub",
    "-a",
    help="Regex filter to apply in processing of the form <match>|||<replacement>",
    action="append",
    default=[],
)
parser.add_argument(
    "--extra_source",
    help="Source to add directly to the output, mainly for includes",
    type=str,
    default="",
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
            s = header.read()
            # turn lin-continuations into single lines, any line ending in backslash
            # newline has the backslash and newline removed
            s = re.sub(r"\\\n", "", s)
            # remove C-style comments, especially inside function declarations
            s = re.sub(r"\/\*([\s\S]*?)\*\/", "", s)
            # attempt to make argument lists single-line
            s = re.sub(r",\s*\n", ", ", s, flags=re.MULTILINE)
            # remove gcc-style attributes
            s = re.sub(r"__attribute__\s*(([^;]*))", "", s)

            for sub in args.add_long_sub:
                [m, r] = sub.split("|||")
                s = re.sub(m, r, s)

            lines = s.split("\n")

            for sub in args.add_sub:
                new_lines = []
                [m, r] = sub.split("|||")
                for l in lines:
                    l = re.sub(m, r, l)
                    new_lines.append(l)
                lines = new_lines

            in_ifdef = False
            in_ifndef = False
            for l in lines:
                # The compiler can't handle the 'extern "C" {' directive,
                # so we need to manually honor the '#ifdef __cplusplus'
                # preprocessor block to make the directive disappear.
                # Assumes no nesting of preprocessor conditionals below
                # __cplusplus conditionals.  Can handle either #ifdef or
                # #ifndef, and the associated #else.
                if in_ifndef:
                    if re.match("#\s*else", l):
                        in_ifndef = False
                        in_ifdef = True
                        continue
                    else:
                        pass  # allow the line to be included
                elif in_ifdef:
                    if re.match("#\s*(endif|else)", l):
                        in_ifdef = False
                    continue
                elif re.match("#\s*ifdef\s+__cplusplus", l):
                    in_ifdef = True
                    continue
                elif re.match("#\s*ifndef\s+__cplusplus", l):
                    in_ifndef = True
                    continue

                m = re.search('#include\s*"([^"]*)"', l)
                if m:
                    nf = find_first(args.search, m.group(1), including_path)
                    process_header(nf, os.path.dirname(os.path.abspath(nf)))
                if not re.match("#\s*(ifdef|ifndef|endif|include|define)", l):
                    mega_header += l + "\n"
        checked_heads[f] = 1


with open("{}_build.py".format(args.modname), "w") as modfile:
    os.chdir(args.path)

    mega_header = ""
    checked_heads = {}

    process_header(absolute_head)

    include_head = args.header
    if args.include_header != "":
        include_head = args.include_header

    mega_header = (
        """
  """
        + mega_header
    )

    ffi_include_base = """
from {module} import ffi as {module}_ffi
ffi.include({module}_ffi)
  """
    ffi_include = ""
    for inc in args.include_ffi:
        ffi_include += ffi_include_base.format(module=inc)

    print(
        '''
#pylint: skip-file
# This is a generated file... linting is less than useful
from cffi import FFI
ffi = FFI()


ffi.set_source('{full_mod}',
            """
#include <{header}>
{extra_source}

void * unpack_long(ptrdiff_t num){{
  return (void*)num;
}}
// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
            libraries=['{library}'])

{includes}

ffi.cdef("""
void * unpack_long(ptrdiff_t num);
void free(void *ptr);


        {cdefs}


    """)
if __name__ == "__main__":
  ffi.emit_c_code('{modname}.c')
    '''.format(
            cdefs=mega_header,
            full_mod=args.modname
            if args.package is None
            else ".".join([args.package, args.modname]),
            modname=args.modname,
            library=args.library,
            header=include_head,
            extra_source=args.extra_source,
            includes=ffi_include,
        ),
        file=modfile,
    )
