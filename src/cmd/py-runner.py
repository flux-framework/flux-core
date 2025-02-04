##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import os
import runpy
import sys
import sysconfig

from _flux._core import ffi, lib


def split_path(path):
    return filter(None, path.split(":"))


def builtin_python_path():
    """
    Get the path to this module and assume this is the builtin
    Python path.
    """
    #  We can't use FLUX_CONF_AUTO since the executable in this context is
    #  just python or python3, therefore determine which builtin conf flag to
    #  use dynamically from the path to the current file.
    #
    #  Note: We need to avoid importing 'flux' here, since that might trigger
    #  the problem this module is trying to solve (user modified PYTHONPATH
    #  bringing incompatible modules first). So we copy the definitions of
    #  FLUX_CONF_INTREE and FLUX_CONF_INSTALLED directly from `lib` here.
    #
    flag = lib.FLUX_CONF_INSTALLED
    if "src/cmd" in __file__:
        flag = lib.FLUX_CONF_INTREE

    return split_path(
        ffi.string(lib.flux_conf_builtin_get(b"python_path", flag)).decode("utf-8")
    )


def flux_python_path_prepend():
    """
    Get the value of FLUX_PYTHONPATH_PREPEND as a list. An empty list is
    returned if the environment variable is not set.
    """
    return split_path(os.environ.get("FLUX_PYTHONPATH_PREPEND", ""))


def prepend_standard_python_paths(patharray):
    """
    Prepend standard interpreter and Flux python module paths to the
    input array `patharray`.

    Paths that will be prepended include, in order:
    - FLUX_PYTHONPATH_PREPEND
    - The Flux builtin path to its own modules
    - The Python interpreter configured values for
      - stdlib
      - platform specific stdlib
      - platform modules
      - pure python modules
    """
    #  Set of paths to append, in order, to sys.path after builtin path:
    std_paths = ("stdlib", "platstdlib", "platlib", "purelib")

    prepend_paths = [
        *flux_python_path_prepend(),
        *builtin_python_path(),
        *[sysconfig.get_path(name) for name in std_paths],
    ]

    for path in prepend_paths:
        try:
            sys.path.remove(path)
        except ValueError:
            pass  # ignore missing path in sys.path

    # Prepend list to sys.path:
    sys.path[0:0] = prepend_paths


def restore_pythonpath():
    """
    Restore PYTHONPATH from the original caller's environment using
    FLUX_PYTHONPATH_ORIG as set by flux(1)
    """
    try:
        val = os.environ["FLUX_PYTHONPATH_ORIG"]
        os.environ["PYTHONPATH"] = val
    except KeyError:
        # If FLUX_PYTHONPATH_ORIG not set, then unset PYTHONPATH:
        del os.environ["PYTHONPATH"]


if __name__ == "__main__":
    #  Pop first argument which is this script, modify sys.path as noted
    #  above, then invoke target script in this interpreter using
    #  runpy.run_path():
    #
    sys.argv.pop(0)
    prepend_standard_python_paths(sys.path)
    restore_pythonpath()
    runpy.run_path(sys.argv[0], run_name="__main__")


# vi: ts=4 sw=4 expandtab
