from pathlib import Path
from cffi import FFI
import os

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
preproc_file = os.path.join(here, "_idset_preproc.h")
core_c_file = os.path.join(here, "_idset.c")

ffi = FFI()

ffi.set_source(
    "_flux._idset",
    """
#include <src/include/flux/core.h>
#include <src/include/flux/idset.h>
#include <src/common/libdebugged/debugged.h>
            """,
    libraries=[
        "flux-core",
        "flux",
        "flux-idset",
        "flux-internal",
        "debugged",
        "flux",
        "idset",
        "util",
    ],
    library_dirs=[
        "/code/src/common/libdebugged/.libs",
        "/code/src/common/libflux/.libs",
        "/code/src/common/libidset/.libs",
        "/code/src/common/libutil/.libs",
        "/code/src/common/.libs",
    ],
    include_dirs=[
        "/code",
        "/code/src/include",
        "/code/src/common/libflux",
        "/code/src/common/libidset",
        "/code/src/common/libdebugged",
        "/code/src/common/libutil",
    ],
    extra_compile_args=[
        "-L/code/src/common/.libs",
        "-L/code/src/common/libdebugged/.libs",
        "-L/code/src/common/libidset/.libs",
        "-L/code/src/common/libflux/.libs",
        "-L/code/src/common/libutil/.libs",
    ],
)

cdefs = """
static const unsigned int IDSET_INVALID_ID;
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
    # Ensure target mtime is updated
    Path(core_c_file).touch()
    ffi.compile(verbose=True)
