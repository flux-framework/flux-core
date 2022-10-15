from pathlib import Path

from cffi import FFI

ffi = FFI()

ffi.set_source(
    "_flux._hostlist",
    """
#include <flux/hostlist.h>


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
    void free (void *);
"""

with open("_hostlist_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_hostlist.c")
    # Ensure target mtime is updated, emit_c_code() may not do it
    Path("_hostlist.c").touch()
