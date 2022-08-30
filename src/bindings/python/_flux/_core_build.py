from pathlib import Path
from cffi import FFI
import os

ffi = FFI()

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
preproc_file = os.path.join(here, "_core_preproc.h")
core_c_file = os.path.join(here, "_core.c")

ffi.set_source(
    "_flux._core",
    """
#include <src/include/flux/core.h>
#include <src/common/libdebugged/debugged.h>


void * unpack_long(ptrdiff_t num){
  return (void*)num;
}
""",
    libraries=["flux-core", "debugged", "flux"],
    library_dirs=[
        "/code/src/common/libdebugged/.libs",
        "/code/src/common/libflux/.libs",
    ],
    include_dirs=[
        "/code",
        "/code/src/include",
        "/code/src/common/libflux",
        "/code/src/common/libdebugged",
    ],
    extra_compile_args=[
        "-L/code/src/common/.libs",
        "-L/code/src/common/libdebugged/.libs",
    ],
)

cdefs = """
typedef int... ptrdiff_t;
typedef int... pid_t;
typedef ... va_list;
void * unpack_long(ptrdiff_t num);
void free(void *ptr);
#define FLUX_JOBID_ANY 0xFFFFFFFFFFFFFFFF

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
