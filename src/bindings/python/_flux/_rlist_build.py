import os
import sys
from pathlib import Path

from cffi import FFI

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))

# We need to add the path here to import other builds
sys.path.insert(0, here)

root = os.environ.get("FLUX_INSTALL_ROOT")

preproc_file = os.path.join(here, "_rlist_preproc.h")
core_c_file = os.path.join(here, "_rlist.c")

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
#include <src/include/flux/idset.h>
#include <src/common/libczmqcontainers/czmq_containers.h>
#include <src/common/libflux/types.h>
#include <src/common/librlist/rlist.h>
#include <src/common/libidset/idset.h>
            """,
    libraries=[
        "rlist",
        "idset",
        "flux-idset",
        "flux-core",
        "flux",
        "flux-internal",
        "hwloc",
        "jansson",
    ],
    library_dirs=[
        f"{root}/src/common/libflux/.libs",
        f"{root}/src/common/librlist/.libs",
        f"{root}/src/common/libidset/.libs",
        f"{root}/src/common/.libs",
    ],
    include_dirs=[
        root,
        "/usr/include",
        # hwloc
        "/usr/lib/x86_64-linux-gnu",
        f"{root}/config",
        f"{root}/src/include",
        f"{root}/src/common/libflux",
        f"{root}/src/common/librlist",
        f"{root}/src/common/libidset",
    ],
    extra_compile_args=[
        f"-L{root}/src/common/.libs",
        f"-L{root}/src/common/librlist/.libs",
        f"-L{root}/src/common/libflux/.libs",
        f"-L{root}/src/common/libidset/.libs",
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
# Ensure target mtime is updated
Path(core_c_file).touch()
ffi.compile(verbose=True)
