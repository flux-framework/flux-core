from pathlib import Path

from _idset_build import ffi as idset_ffi
from cffi import FFI

ffi = FFI()

ffi.include(idset_ffi)

ffi.set_source(
    "_flux._count",
    """
#include <limits.h>
#include <stdbool.h>
#include <jansson.h>
#include "src/common/libjob/count.h"


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
typedef struct json_t json_t;
typedef struct json_error_t json_error_t;
static const unsigned int COUNT_MAX;
static const unsigned int COUNT_INVALID_VALUE;
void free (void *);
"""

with open("_count_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_count.c")
    # Ensure target mtime is updated
    Path("_count.c").touch()
