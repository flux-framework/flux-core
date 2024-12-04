##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import logging
import os
import sys

import flux
from flux.uri import FluxURIResolver

LOGGER = logging.getLogger("flux-uri")


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    resolver = FluxURIResolver()
    plugins = [f"  {x}\t\t{y}" for x, y in resolver.plugins().items()]
    plugin_list = "Supported resolver schemes:\n" + "\n".join(plugins)

    parser = argparse.ArgumentParser(
        prog="flux-uri",
        description="Resolve TARGET to a Flux URI",
        epilog=plugin_list,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--remote",
        action="store_true",
        help="convert a local URI to remote",
    )
    parser.add_argument(
        "--local",
        action="store_true",
        help="convert a remote URI to local",
        default=os.environ.get("FLUX_URI_RESOLVE_LOCAL"),
    )
    parser.add_argument(
        "--wait",
        action="store_true",
        help="wait for a URI to become available, if supported",
    )
    parser.add_argument(
        "uri",
        metavar="TARGET",
        help="A Flux jobid or URI in scheme:argument form (e.g. jobid:f1234)",
    )

    args = parser.parse_args()
    if args.wait:
        args.uri = args.uri + "?wait"
    uri = resolver.resolve(args.uri)
    if args.remote:
        print(uri.remote)
    elif args.local:
        print(uri.local)
    else:
        print(uri)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
