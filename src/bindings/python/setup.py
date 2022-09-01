###############################################################
# Copyright 2014-2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage
# This should work with setup.py and pip, and arguments are provided as follows:
# python setup.py install --path=/path/to/flux
# pip install --install-option="--path=/path/to/flux" .

import os
import re
import shutil
import subprocess
import sys
from contextlib import contextmanager

from setuptools import find_packages
from setuptools import setup as _setup
from setuptools.command.install import install

# Metadata
package_name = "flux"
package_version = "0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1a1"
package_description = "Bindings to the flux resource manager API"
package_url = "https://github.com/flux-framework/flux-core"
package_keywords = "flux, job manager, orchestration, hpc"

# Setup variables for dependencies
cffi_dep = "cffi>=1.1"

# src/bindings/python
here = os.path.dirname(os.path.abspath(__file__))

# top level with src, etc (/code here)
root = os.path.dirname(os.path.dirname(os.path.dirname(here)))

# Module specific default options files. Format strings below will be populated
# after receiving the custom varibles from the user
options = {
    "core": {
        "path": "{root}",
        "search": [os.path.join("{root}", "src", "common", "libflux")],
        "header": "src/include/flux/core.h",
        "additional_headers": [
            "src/bindings/python/_flux/callbacks.h",
            "src/common/libdebugged/debugged.h",
        ],
    },
    "hostlist": {
        "path": "{root}/src/common/libhostlist",
        "header": "src/include/flux/hostlist.h",
    },
    "rlist": {
        "path": "{root}/src/common/librlist",
        "search": [
            "{root}",
            os.path.join("{root}", "config"),
        ],
        "header": "src/common/librlist/rlist.h",
        "ignore_headers": ["czmq_containers"],
    },
    "idset": {
        "path": "{root}/src/common/libidset",
        "header": "src/common/libidset/idset.h",
    },
    # path and header are set by --flux-security
    "security": {},
}

# Global variables for build type, corresponds to
build_types = {"core"}

# Cut out early if not providing full detail about security
def check_security_args():
    """
    Ensure --security is provided with --security-src and --security-include
    """
    if "--security" not in sys.argv:
        return True
    found = {}
    for arg in sys.argv:
        if not arg.startswith("--"):
            continue
        arg = arg.split("=")[0].strip()
        found[arg] = True
    return "--security-src" in found and "--security-include" in found


if not check_security_args():
    sys.exit(
        "--security-include and --security-src are required when building security module."
    )


@contextmanager
def workdir(dirname):
    """
    Allow a temporary working directory to run commands
    """
    original = os.getcwd()
    os.chdir(dirname)
    try:
        yield
    finally:
        os.chdir(original)


def read_file(filename):
    """
    Read a filename into a text blob.
    """
    with open(filename, "r") as fd:
        data = fd.read()
    return data


def find_first(path, name, extra=None):
    """
    Find the first of a name that appears in a path.
    """
    for dirname in path:
        filename = os.path.join(dirname, name)
        if os.path.isfile(filename):
            return filename
    if extra is not None:
        filename = os.path.join(extra, name)
        if os.path.isfile(filename):
            return filename
    raise IOError(name)


class PrepareFluxHeaders(install):
    """
    Custom setuptools install command prepared cleaned headers.

    This currently requires the Python install to live alongside Flux,
    but is a first step to removing it from the automake system and having
    a proper setup.py. We might eventually be able to separate them further.
    """

    user_options = install.user_options + [
        ("flux-root=", None, "Root of Flux source code"),
        ("search=", None, "comma separated list to append to header search path"),
        # This can eventually allow pointing pip to pre-compiled headers?
        ("skip-build", None, "Skip building headers"),
        # Only required if building security
        (
            "security-include=",
            None,
            "path to flux security include directory (only for --security module)",
        ),
        (
            "security-src=",
            None,
            "path to security source directory (where you cloned it)",
        ),
        # These are additional modules to build, provided as flags
        ("hostlist", None, "Build hostlist module"),
        ("rlist", None, "Build rlist module (also builds idset)"),
        ("idset", None, "Build idset module"),
        ("security", None, "Build security module"),
    ]

    def initialize_options(self):
        """
        Initialize options - they are fully set later based on build type
        """
        install.initialize_options(self)
        self.flux_root = root
        self.search = ""
        self.skip_build = False

        # I don't think this is being used
        self.include_header = None

        # Modules
        self.hostlist = False
        self.rlist = False
        self.idset = False
        self.security = False
        self.security_include = None
        self.security_src = None

    def finalize_options(self):
        """
        Finalize options, showing user what was set.
        """
        self.set_builds()
        # If we have additional headers or ignore headers, ensure list
        for attr in ["search"]:
            self._parse_comma_list(attr)
        install.finalize_options(self)

        # Update envars to be seen by build modules
        self.set_envar("FLUX_INSTALL_ROOT", self.flux_root)
        if self.security_src:
            self.set_envar("FLUX_SECURITY_SOURCE", self.security_src)
        if self.security_include:
            self.set_envar("FLUX_SECURITY_INCLUDE", self.security_include)

    def set_envar(self, key, value):
        """
        Set an environment variable.

        There isn't another good way to communicate with build modules.
        """
        os.putenv(key, value)
        os.environ[key] = value

    def set_builds(self):
        """
        Given user preferences on the command line, set build flags
        for additional modules.
        """
        global build_types
        if self.hostlist:
            build_types.add("hostlist")
        if self.rlist:
            build_types.add("rlist")
        if self.idset:
            build_types.add("idset")
        if self.security:
            build_types.add("security")
            options["security"]["path"] = self.security_include
            options["security"]["header"] = os.path.join(
                self.security_src, "src", "lib", "sign.h"
            )

    def _parse_comma_list(self, attr):
        """
        Given an attribute (user argument) convert string with csv to list
        """
        value = getattr(self, attr, None)
        if value:
            value = value.split(",")
        elif not value:
            value = []
        setattr(self, attr, value)

    def run(self):
        """
        Run the install
        """
        if not self.skip_build:
            for build_type in build_types:
                cleaner = HeaderCleaner(
                    self.flux_root,
                    custom_search=self.search,
                    include_header=self.include_header,
                    build_type=build_type,
                    **options[build_type],
                )
                cleaner.clean_headers()
        install.run(self)  # OR: install.do_egg_install(self)


class HeaderCleaner:
    def __init__(self, root, include_header, custom_search, build_type, **kwargs):
        """
        Main class to run a clean!
        """
        self.options = [
            "flux_root",
            "search",
            "skip_build",
            "include_header",
            "hostlist",
            "rlist",
            "idset",
            "security",
            "security_include",
            "security_src",
        ]
        self.root = root
        self.path = kwargs["path"].format(root=root)
        self.build_type = build_type
        self.include_header = include_header
        self.preproc_output = os.path.join(here, "_flux", "_%s_preproc.h" % build_type)
        self.output = os.path.join(here, "_flux", "_%s_clean.h" % build_type)

        # Relative path to header is required
        self.header = kwargs["header"]

        # Update search to include defaults
        self.search = custom_search + [
            x.format(root=root) for x in kwargs.get("search", [])
        ]

        # Not required
        self.additional_headers = kwargs.get("additional_headers", [])
        self.ignore_headers = kwargs.get("ignore_headers", [])
        self.show_options()

    def show_options(self):
        """
        Show build options to the user for clarity.
        """
        # This will not show up with pip, but it's running
        for opt in self.options:
            if hasattr(self, opt) and getattr(self, opt) is not None:
                value = getattr(self, opt)
                print("%s: %s" % (opt.rjust(20), value))

    def clean_headers(self):
        """
        Prepare cleaned headers for cffi
        """
        # Reset checked headers and final "mega header"
        self.checked_heads = {}
        self.mega_header = ""

        # Prepend 'path' option to search list:
        self.search.insert(0, self.path)
        self.search = [os.path.abspath(f) for f in self.search]
        with workdir(self.root):
            self.process_header()

            # Process additional headers
            for header in self.additional_headers or []:
                self.process_header(header)

        # Note that this currently isn't used - should it passed to
        # the build script?
        include_head = self.header
        if self.include_header:
            include_head = self.include_header

        # Write the clean header!
        print(f"Writing stage 1 clean header to {self.output}")
        with open(self.output, "w") as clean_header:
            clean_header.write(self.mega_header)

        # -E '-D__attribute__(...)=' and re-read
        self.preprocess_gcc(self.output)
        self.mega_header = read_file(self.output)

        # Remove compiler directives
        self.clean_compiler_directives()

    def preprocess_gcc(self, filename):
        """
        Compile with gcc -E '-D__attribute__(...)='
        """
        gcc = shutil.which("gcc")
        if not gcc:
            sys.exit("Cannot find gcc compiler.")
        cmd = [
            gcc,
            "-E",
            "-D__attribute__(...)=",
            self.output,
            "-o",
            self.preproc_output,
        ]
        print(" ".join(cmd))
        res = subprocess.call(cmd)
        if res != 0:
            sys.exit("Issue preprocessing header to %s" % self.preproc_output)

    def clean_compiler_directives(self):
        """
        Original sed: sed -e '/^# [0-9]*.*/d'
        """
        cleaned = []
        for line in self.mega_header.split("\n"):
            if not re.search("^# [0-9]*.*", line):
                cleaned.append(line)
        self.mega_header = "\n".join(cleaned)

    def process_header(self, f=None, including_path="."):
        """
        Process header
        """
        # If called for the first time, this is the "absolute header"
        if not f:
            f = os.path.abspath(os.path.join(self.root, self.header))
        if not os.path.isfile(f):
            f = os.path.join(including_path, f)
        f = os.path.abspath(f)

        # Bail out early if we've already checked it
        if f in self.checked_heads:
            return

        # Set as checked if we want to ignore it
        for p in self.ignore_headers or []:
            if re.search(p, f):
                self.checked_heads[f] = 1
                return

        # If we get here, we aren't ignoring it! Add to mega header
        self.check_header(f, including_path)

    def check_header(self, f, including_path="."):
        """
        Given a header file, f, recursively check it.
        """
        with open(f, "r") as header:
            for l in header.readlines():
                m = re.search('#include\s*"([^"]*)"', l)
                if m:
                    nf = find_first(self.search, m.group(1), including_path)
                    self.process_header(nf, os.path.dirname(os.path.abspath(nf)))
                if not re.match("#\s*include", l):
                    self.mega_header += l

        # Flag as checked
        self.checked_heads[f] = 1


def clean_args():
    """
    Ensure we remove extra flags that the second installed won't know about.
    """
    argregex = "(--security-src|--security-include)"
    cleaned = []
    for arg in sys.argv:
        if not re.search(argregex, arg):
            cleaned.append(arg)
    sys.argv = cleaned


# Setup.py logic goes here


def setup():
    """
    A wrapper to run setup. This likely isn't best practice, but is a first effort.
    """
    # Custom setup commands, first without cffi to prepare headers
    _setup(
        name=package_name,
        version=package_version,
        description=package_description,
        cmdclass={
            "build_ext": PrepareFluxHeaders,
            "install": PrepareFluxHeaders,
            "develop": PrepareFluxHeaders,
        },
    )

    # Request to install additional modules (we always do core0
    # We also have to remove the setup.py flags that aren't known
    cffi_modules = ["_flux/_core_build.py:ffi"]
    for build_type in build_types:

        # We always include / require core (may not be necessary)
        if build_type == "core":
            continue
        cffi_modules.append("_flux/_%s_build.py:ffi" % build_type)
        sys.argv.pop(sys.argv.index(f"--{build_type}"))

    # Remove extra security args
    clean_args()

    print("cffi_modules:\n%s" % "\n".join(cffi_modules))

    # This assumes relative location of Flux install
    # Now with cffi for final install
    _setup(
        name=package_name,
        version=package_version,
        description=package_description,
        keywords=package_keywords,
        url=package_url,
        setup_requires=[cffi_dep],
        packages=find_packages(),
        include_package_data=True,
        zip_safe=False,
        install_requires=[cffi_dep],
        extras_require={
            "dev": ["pyyaml", "jsonschema", "docutils", "black", "IPython"]
        },
        classifiers=[
            "Intended Audience :: Science/Research",
            "Intended Audience :: Developers",
            "License :: OSI Approved :: GNU Lesser General Public License v3 or later (LGPLv3+)",
            "Programming Language :: C++",
            "Programming Language :: Python",
            "Topic :: Software Development",
            "Topic :: Scientific/Engineering",
            "Operating System :: Unix",
            "Programming Language :: Python :: 3.8",
        ],
        cffi_modules=cffi_modules,
    )


setup()
