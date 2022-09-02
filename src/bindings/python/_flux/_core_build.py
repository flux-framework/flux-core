import os
from pathlib import Path

from cffi import FFI

ffi = FFI()

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
root = os.environ.get("FLUX_INSTALL_ROOT")

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
        f"{root}/src/common/.libs",
        f"{root}/src/common/libdebugged/.libs",
        f"{root}/src/common/libflux/.libs",
    ],
    include_dirs=[
        root,
        f"{root}/src/include",
        f"{root}/src/common/libflux",
        f"{root}/src/common/libdebugged",
    ],
    extra_compile_args=[
        f"-L{root}/src/common/.libs",
        f"-L{root}/src/common/libflux/.libs",
        f"-L{root}/src/common/libdebugged/.libs",
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
ffi.emit_c_code(core_c_file)

# ensure mtime of target is updated
Path(core_c_file).touch()
ffi.compile(verbose=True)
