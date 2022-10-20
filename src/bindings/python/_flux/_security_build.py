from pathlib import Path

from cffi import FFI

ffi = FFI()


ffi.set_source(
    "_flux._security",
    """
#include <flux/security/sign.h>


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
typedef int... ptrdiff_t;
typedef int... pid_t;
typedef ... va_list;
void free(void *ptr);

    """

with open("_security_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_security.c")
    # ensure target mtime is updated
    Path("_security.c").touch()
