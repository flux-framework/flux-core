import os
import sys
from pathlib import Path

from cffi import FFI

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
root = os.path.dirname(here)

# Allow to import options from setup.py
sys.path.insert(0, root)
from setup import root

preproc_file = os.path.join(here, "_hostlist_preproc.h")
core_c_file = os.path.join(here, "_hostlist.c")

ffi = FFI()

##include <flux/hostlist.h>

ffi.set_source(
    "_flux._hostlist",
    """
#include <src/include/flux/hostlist.h>
#include <src/common/libhostlist/hostlist.h>
            """,
    libraries=["flux-core", "flux", "hostlist", "flux-hostlist", "flux-internal"],
    library_dirs=[
        f"{root}/src/common/libflux/.libs",
        f"{root}/src/common/libhostlist/.libs",
        f"{root}/src/common/.libs",
    ],
    include_dirs=[
        root,
        f"{root}/src/include",
        f"{root}/src/common/libflux",
    ],
    extra_compile_args=[
        f"-L{root}/src/common/.libs",
        f"-L{root}/src/common/libhostlist/.libs",
        f"-L{root}/src/common/libflux/.libs",
    ],
)

cdefs = """
    void free (void *);
"""

with open(preproc_file) as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)

# If this is in main it's not called by setuptools
ffi.emit_c_code(core_c_file)
Path(core_c_file).touch()
ffi.compile(verbose=True)
