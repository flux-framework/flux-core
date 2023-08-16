#!/usr/bin/env python3

# Generate codegen json (dumps of cffi functions)
# or generate classes from them. Example usage:
#
## This generates ./codegen/_core.json
# python3 codegen.py dump --out ./codegen _core.so
#
## This generates wrapper/core.py
# python3 codegen.py generate --out ../flux/wrapper ./codegen/_core.json

import argparse
import os
import sys
import re
import importlib
import json

here = os.path.abspath(os.path.dirname(__file__))

# base template for a wrapper. We add the class functions to it.
template = """###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from .base import Wrapper


"""


def get_parser():
    parser = argparse.ArgumentParser(
        description="Generate codegen output for cffi",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    subparsers = parser.add_subparsers(
        help="actions",
        title="actions",
        dest="command",
    )
    gen = subparsers.add_parser(
        "generate", description="dump codegen.json output for .so files"
    )
    dump = subparsers.add_parser(
        "dump", description="generate codegen clas files from codegen json"
    )
    for command in gen, dump:
        command.add_argument(
            "--out",
            help="output directory",
            default=os.path.join(here, "codegen"),
        )
    return parser


def generate_class(name):
    """
    generate a named Python class (expecting functions)
    """
    return "class %sWrapper(Wrapper):\n" % name.capitalize()


def generate_args(args):
    """
    Given a list of args, format into a string
    """
    argstr = ""
    for i, arg in enumerate(args):
        argname = re.sub("([*]|struct| |[)]|[(]|_)", "", arg['cname']).lower()

        # Special cases of args (only one handle)
        if "handle" in argname:
            argname = "handle"

        # Always add the index to ensure unique
        argname = f"{argname}{i}"
        argstr += f", {argname}"
    return argstr


def get_spacing(indentation=1):
    """
    Get spacing at some indentation, where each is 4 spaces
    """
    return "    " * indentation


def generate_function(name, meta):
    """
    Given a name and metadata (args), generate a class function
    """
    body = f"{get_spacing()}def {name}(self"
    args = meta.get('args', []) or []
    argstr = generate_args(args)

    # Code spacing
    s = get_spacing(2)
    body += argstr + "):\n"
    body += f'{s}"""Wrapper to C function {name}"""\n'
    # This is for debugging, if needed
    # body += f"{s}print(\"{name}\")\n{s}import IPython\n{s}IPython.embed()\n"
    returns = argstr.strip(',').strip()
    body += f"{s}return getattr(self.lib, \"{name}\")({returns})\n\n"
    return body


class SoFile:
    """
    Interact with an sofile, exposing functions and saving to codegen.
    """

    def __init__(self, sofile):
        self.sofile = sofile
        self.abspath = os.path.abspath(sofile)

    @property
    def module_name(self):
        return self.sofile.split(".so", 1)[0]

    def load(self):
        """
        Given an sofile path, import as cffi module.
        """
        dirname = os.path.dirname(self.abspath)
        os.chdir(dirname)
        module = importlib.import_module(self.module_name)
        os.chdir(here)
        return module

    def codegen(self):
        """
        Generate codegen for the module
        """
        module = self.load()
        ffi = module.ffi
        gen = {}
        for function_name in dir(module.lib):
            # The only thing we can get is the original signature
            try:
                func = getattr(module.lib, function_name)
            except Exception as e:
                print(f"Issue with getting function {function_name}: {e} (skipping)")
                continue

            # If it's a constant type or variadic, this will error
            try:
                fun_type = ffi.typeof(func)
            except Exception as e:
                # This would indicate it's presence (disabled)
                # gen[function_name] = {}
                print(f"Issue with getting type for {function_name}: {e} (skipping)")
                continue

            # Generate signature for the function
            signature = {"cname": fun_type.cname, "type": fun_type.kind, "args": []}
            for arg in fun_type.args:
                signature["args"].append({"cname": arg.cname, "kind": arg.kind})
            signature["result"] = {
                "cname": fun_type.result.cname,
                "kind": fun_type.result.kind,
            }
            gen[function_name] = signature
        return gen


def write_json(obj, out):
    """
    Write json to file
    """
    with open(out, "w") as fd:
        fd.write(json.dumps(obj, indent=4))


def write_file(obj, out):
    """
    Write text file file
    """
    with open(out, "w") as fd:
        fd.write(obj)


def read_json(filename):
    """
    Read json from file
    """
    with open(filename, "r") as fd:
        content = json.loads(fd.read())
    return content


def generate_codegen_json(sofiles, out):
    """
    Given a list of one or more sofiles, generate json codegen
    """
    for sofile in sofiles:
        if not os.path.exists(sofile):
            print(f'WARNING: {sofile} does not exist.')
            continue
        try:
            so = SoFile(sofile)
        except Exception as e:
            sys.exit(f"There was an issue opening {sofile}: {e}")
        result = so.codegen()
        outfile = os.path.join(out, f"{so.module_name}.json")
        write_json(result, outfile)


def generate_wrappers(files, out):
    """
    Given a list of json input files, generate python wrapper classes
    """
    out = os.path.abspath(out)
    for filename in files:
        filename = os.path.abspath(filename)
        if not os.path.exists(filename):
            print(f'WARNING: {filename} does not exist.')
            continue
        lib = read_json(filename)

        # The name of the module
        module_name = os.path.basename(filename).split('.')[0].replace('_', '')
        cls = template + generate_class(module_name)

        # Add on functions. These are all positional args
        for function_name, meta in lib.items():
            cls += generate_function(function_name, meta)

        # Clean up leading or trailing white space
        cls = cls.strip()
        outclass = os.path.join(out, f"{module_name}.py")
        write_file(cls, outclass)


def main():
    """
    Generate codegen information about a library.
    """
    parser = get_parser()

    # Trust the user to provide so files (don't validate extension)
    args, files = parser.parse_known_args()

    if not files:
        sys.exit("Please provide one or more *.so files as positional arguments.")
    # If the output directory does not exist, create it
    if not os.path.exists(args.out):
        os.makedirs(args.out)

    # The only option right now is to generate the codegen files
    if args.command == "dump":
        return generate_codegen_json(files, args.out)
    if args.command == "generate":
        return generate_wrappers(files, args.out)
    parser.print_usage()


if __name__ == "__main__":
    main()
