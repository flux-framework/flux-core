##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

# flux-module-python-exec - load and run a Python broker module
#
# This program is used as a module loader by the Flux broker when loading
# Python broker modules (.py suffix).  It implements the broker module
# loader protocol described in RFC 5:
#
#   1. Open a handle using FLUX_MODULE_URI from the environment.
#   2. Initialize the module via flux_module_initialize().
#   3. Register standard module handlers via flux_module_register_handlers().
#   4. Dynamically import the Python module at PATH and call mod_main(h, *args).
#      The Python module is responsible for running the reactor.
#   5. Finalize the module via flux_module_finalize().
#
# The Python module must define either:
#
#   def mod_main(h, *args): ...
#
# or a single BrokerModule subclass, in which case mod_main is synthesized as
# BrokerModuleSubclass(h, *args).run().  h is a flux.Flux handle and args are
# the (possibly empty) module args split from the space-delimited string
# returned by flux_module_initialize().
#
# Usage: flux module-python-exec PATH

import errno as errno_mod
import os
import sys
import traceback

from _flux._core import ffi, lib
from flux.brokermod import resolve_entry_point
from flux.core.handle import Flux
from flux.importer import import_path
from flux.proctitle import set_proctitle


def die(msg):
    print(f"{os.path.basename(sys.argv[0])}: {msg}", file=sys.stderr)
    sys.exit(1)


def die_error(msg, error):
    text = ffi.string(error.text).decode("utf-8", errors="replace")
    die(f"{msg}: {text}")


def main():
    if len(sys.argv) != 2:
        die(f"Usage: {os.path.basename(sys.argv[0])} PATH")
    path = sys.argv[1]
    uri = os.environ.get("FLUX_MODULE_URI")
    if not uri:
        die("FLUX_MODULE_URI is not set")

    try:
        h = Flux(url=uri)
    except OSError as exc:
        die(f"flux_open: {exc}")

    error = ffi.new("flux_error_t[1]")
    args_p = ffi.new("char **")
    if lib.flux_module_initialize(h.handle, args_p, error) < 0:
        die_error("flux_module_initialize", error[0])

    modargs = None
    if args_p[0] != ffi.NULL:
        modargs = ffi.string(args_p[0]).decode("utf-8")
        lib.free(args_p[0])

    name_p = ffi.cast("char *", lib.flux_aux_get(h.handle, b"flux::name"))
    if name_p != ffi.NULL:
        set_proctitle(ffi.string(name_p).decode())

    if lib.flux_module_register_handlers(h.handle, error) < 0:
        die_error("flux_module_register_handlers", error[0])

    mod = import_path(path)

    args = modargs.split() if modargs else []

    try:
        mod_main = resolve_entry_point(mod)
    except ValueError as exc:
        die(f"{path}: {exc}")

    errnum = 0
    try:
        mod_main(h, *args)
    except ValueError as exc:
        # User-facing argument errors: print message only, no traceback
        print(f"{os.path.basename(path)}: {exc}", file=sys.stderr)
        errnum = errno_mod.EINVAL
    except OSError as exc:
        # Module exited with error already logged via the broker log.
        # Propagate the original errno if available so the caller sees a
        # meaningful error code (e.g. EEXIST for service already registered).
        errnum = exc.errno if exc.errno else errno_mod.ECONNRESET
    except Exception:  # pylint: disable=broad-except
        traceback.print_exc()
        errnum = errno_mod.ECONNRESET

    error = ffi.new("flux_error_t[1]")
    if lib.flux_module_finalize(h.handle, errnum, error) < 0:
        die_error("flux_module_finalize", error[0])


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
