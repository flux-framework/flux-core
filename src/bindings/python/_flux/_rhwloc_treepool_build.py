from pathlib import Path

from cffi import FFI

ffi = FFI()

ffi.set_source(
    "_flux._rhwloc_treepool",
    """
#include "src/common/librlist/rhwloc_treepool.h"

// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
    """,
)

ffi.cdef(
    """
typedef struct {
    char text[160];
} flux_error_t;

char *rhwloc_treepool_topo_to_json (const char *xml, flux_error_t *errp);

void free (void *);
"""
)

if __name__ == "__main__":
    ffi.emit_c_code("_rhwloc_treepool.c")
    Path("_rhwloc_treepool.c").touch()
