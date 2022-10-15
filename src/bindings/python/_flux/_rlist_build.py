from pathlib import Path

from _hostlist_build import ffi as hostlist_ffi
from _idset_build import ffi as idset_ffi
from cffi import FFI

ffi = FFI()

ffi.include(hostlist_ffi)
ffi.include(idset_ffi)

ffi.set_source(
    "_flux._rlist",
    """
#include <jansson.h>
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libflux/types.h"
#include "src/common/librlist/rlist.h"


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
typedef struct _zlistx_t zlistx_t;
typedef struct _zhashx_t zhashx_t;
typedef int... json_type;
typedef struct json_t json_t;
typedef struct json_error_t json_error_t;


void free (void *);

"""

with open("_rlist_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_rlist.c")
    # Ensure target mtime is updated
    Path("_rlist.c").touch()
