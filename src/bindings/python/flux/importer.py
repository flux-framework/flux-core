###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import importlib
import os
import pkgutil
import sys


def import_plugins_pkg(ns_pkg):
    """Import all modules found in the namespace package ``ns_pkg``"""
    result = {}
    for finder, name, ispkg in pkgutil.iter_modules(ns_pkg.__path__):
        try:
            result[name] = importlib.import_module(f"{ns_pkg.__name__}.{name}")
        except ImportError as exc:
            raise ImportError(f"failed to import {name} plugin: {exc}")
    return result


def import_plugins(pkg_name, pluginpath=None):
    """Load plugins from a namespace package and optional additional paths

    A plugin in pluginpath with the same name as an existing plugin will
    take precedence
    """
    if pluginpath is not None:
        sys.path[1:1] = pluginpath

    try:
        #  Load 'pkg_name' as a namespace plugin
        #
        #  Note: importlib.import_module() will raise ModuleNotFoundError
        #  if pkg_name points to an empty directory, but we just want to
        #  return an empty list in this case:
        #
        pkg = importlib.import_module(pkg_name)
    except ModuleNotFoundError:
        return {}

    plugins = import_plugins_pkg(pkg)

    if pluginpath is not None:
        #  Undo any added pluginpath elements.
        for path in pluginpath:
            sys.path.remove(path)

    return plugins


def import_path(file_path):
    """Import a module directly from file_path"""

    module_name = os.path.basename(file_path).rstrip(".py")
    spec = importlib.util.spec_from_file_location(module_name, file_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
