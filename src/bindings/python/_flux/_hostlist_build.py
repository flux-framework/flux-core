from cffi import FFI
from pathlib import Path
import os

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
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
        "/code/src/common/libflux/.libs",
        "/code/src/common/libhostlist/.libs",
        "/code/src/common/.libs",
    ],
    include_dirs=[
        "/code",
        "/code/src/include",
        "/code/src/common/libflux",
    ],
    extra_compile_args=[
        "-L/code/src/common/.libs",
        "-L/code/src/common/libhostlist/.libs",
        "-L/code/src/common/libflux/.libs",
    ],
)

cdefs = """
    void free (void *);
"""

with open(preproc_file) as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)

# This doesn't seem to happen in the block below
ffi.emit_c_code(core_c_file)
ffi.compile(verbose=True)

if __name__ == "__main__":
    ffi.emit_c_code(core_c_file)
    # ensure mtime of target is updated
    Path(core_c_file).touch()
    ffi.compile(verbose=True)
