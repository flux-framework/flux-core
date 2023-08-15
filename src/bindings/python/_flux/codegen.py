#!/usr/bin/env python3

import argparse
import os
import sys
import importlib
import json

here = os.path.abspath(os.path.dirname(__file__))


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
        "generate", description="generate codegen.json output for .so files"
    )
    gen.add_argument(
        "--out",
        help="output directory for json files",
        default=os.path.join(here, "codegen"),
    )
    return parser


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


def main():
    """
    Generate codegen information about a library.
    """
    parser = get_parser()

    # Trust the user to provide so files (don't validate extension)
    args, sofiles = parser.parse_known_args()

    if not sofiles:
        sys.exit("Please provide one or more *.so files as positional arguments.")

    # If the output directory does not exist, create it
    if not os.path.exists(args.out):
        os.makedirs(args.out)

    # The only option right now is to generate the codegen files
    if args.command == "generate":
        return generate_codegen_json(sofiles, args.out)
    parser.print_usage()


if __name__ == "__main__":
    main()
