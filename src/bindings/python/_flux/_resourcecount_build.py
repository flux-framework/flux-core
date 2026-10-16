from pathlib import Path

from cffi import FFI

ffi = FFI()

ffi.set_source(
    "_flux._resourcecount",
    """
#include <flux/resourcecount.h>


// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
            """,
)

cdefs = """
static const unsigned int RESOURCECOUNT_MAX;
static const unsigned int RESOURCECOUNT_INVALID_VALUE;

/* resourcecount.h defines RESOURCECOUNT_FLAG_* in terms of IDSET_FLAG_*, which
 * are enum constants rather than macros, so the preprocessor cannot expand them.
 * Redeclare idset_flags here so that the cdef parser can resolve them.
 */
enum idset_flags {
    IDSET_FLAG_AUTOGROW = 1,
    IDSET_FLAG_BRACKETS = 2,
    IDSET_FLAG_RANGE = 4,
    IDSET_FLAG_INITFULL = 8,
    IDSET_FLAG_COUNT_LAZY = 16,
    IDSET_FLAG_ALLOC_RR = 32,
};

/* Opaque: resourcecount_create() is not called from python, and jansson
 * has changed the declaration of json_error_t between releases.
 */
typedef ... json_t;
typedef ... json_error_t;

void free (void *);
"""

with open("_resourcecount_preproc.h") as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
if __name__ == "__main__":
    ffi.emit_c_code("_resourcecount.c")
    # Ensure target mtime is updated
    Path("_resourcecount.c").touch()
