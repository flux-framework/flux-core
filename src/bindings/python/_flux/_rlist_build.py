from pathlib import Path
from cffi import FFI
import sys
import os

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))
preproc_file = os.path.join(here, "_rlist_preproc.h")
core_c_file = os.path.join(here, "_rlist.c")

sys.path.insert(0, here)

from _hostlist_build import ffi as hostlist_ffi
from _idset_build import ffi as idset_ffi


ffi = FFI()

ffi.include(hostlist_ffi)
ffi.include(idset_ffi)


ffi.set_source(
    "_flux._rlist",
    """
#include <jansson.h>
#include <src/include/flux/core.h>
#include <src/common/libczmqcontainers/czmq_containers.h>
#include <src/common/libflux/types.h>
#include <src/common/librlist/rlist.h>
#include <src/common/libutil/cronodate.h>
#include <src/include/flux/idset.h>
#include <src/include/flux/hostlist.h>
#include <src/common/libhostlist/hostlist.h>
#include <src/common/libdebugged/debugged.h>
            """,
    libraries=[
        "flux-core",
        "flux",
        "flux-idset",
        "hostlist",
        "flux-hostlist",
        "flux-internal",
        "rlist",
        "util",
    ],
    library_dirs=[
        "/code/src/common/libflux/.libs",
        "/code/src/common/libhostlist/.libs",
        "/code/src/common/librlist/.libs",
        "/code/src/common/libutil/.libs",
        "/code/src/common/libidset/.libs",
        "/code/src/common/.libs",
    ],
    include_dirs=[
        "/code",
        "/usr/include",
        "/code/config",
        "/code/src/include",
        "/code/src/common/libutil",
        "/code/src/common/libflux",
        "/code/src/common/librlist",
        "/code/src/common/libidset",
    ],
    extra_compile_args=[
        "-L/code/src/common/.libs",
        "-L/code/src/common/libhostlist/.libs",
        "-L/code/src/common/librlist/.libs",
        "-L/code/src/common/libutil/.libs",
        "-L/code/src/common/libflux/.libs",
        "-L/code/src/common/libidset/.libs",
    ],
)

cdefs = """
typedef struct _zlistx_t zlistx_t;
typedef struct _zhashx_t zhashx_t;
typedef int... json_type;
typedef struct json_t json_t;
typedef struct json_error_t json_error_t;


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
