from pathlib import Path

from cffi import FFI

ffi = FFI()

ffi.set_source(
    "_flux._idset",
    """
#include <flux/idset.h>


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
static const unsigned int IDSET_INVALID_ID;
void free (void *);
"""

with open("_idset_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_idset.c")
    # Ensure target mtime is updated
    Path("_idset.c").touch()
