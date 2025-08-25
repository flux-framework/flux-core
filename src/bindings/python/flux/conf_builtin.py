###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import threading
from pathlib import Path

from flux.core.inner import raw

tls = threading.local()


def _conf_builtin_get_flag():
    """Simulate the use of FLUX_CONF_AUTO for Python

    FLUX_CONF_AUTO will not work from Python since the executable will
    be python and not something part of the Flux build tree or installed
    by Flux. This function simulates FLUX_CONF_AUTO by returning the
    correct FLUX_CONF_FLAG based on whether this module is under the
    in tree PYTHONPATH or not.
    """
    if not hasattr(tls, "FLUX_CONF_AUTO_FLAG"):
        # Resolve builtin installed python path:
        pythonpath = conf_builtin_get("python_path", which="intree").split(":")
        for path in pythonpath:
            if Path(path).resolve() in Path(__file__).resolve().parents:
                # If path is one of this module's parents,
                # then this module is in tree:
                tls.FLUX_CONF_AUTO_FLAG = raw.FLUX_CONF_INTREE
                return tls.FLUX_CONF_AUTO_FLAG
        # O/w, assume we're installed
        tls.FLUX_CONF_AUTO_FLAG = raw.FLUX_CONF_INSTALLED
    return tls.FLUX_CONF_AUTO_FLAG


def conf_builtin_get(name, which="auto"):
    """Get builtin (compiled-in) configuration values from libflux

    Args:
        name (str): name of config value
        which (str): one of "installed", "intree", or "auto" to return
            the installed path, in tree path, or automatically determine
            which to use. default=auto.
    """
    if which == "auto":
        flag = _conf_builtin_get_flag()
    elif which == "installed":
        flag = raw.FLUX_CONF_INSTALLED
    elif which == "intree":
        flag = raw.FLUX_CONF_INTREE
    else:
        raise ValueError("which must be one of auto, installed, or intree")

    try:
        return raw.flux_conf_builtin_get(name, flag).decode("utf-8")
    except OSError:
        raise ValueError(f"No builtin config value for '{name}'")
