from pathlib import Path

from cffi import FFI

ffi = FFI()

ffi.set_source(
    "_flux._rhwloc_map",
    """
#include "src/common/librlist/rhwloc_map.h"

// TODO: remove this when we can use cffi 1.10
#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
    """,
)

ffi.cdef(
    """
typedef struct rhwloc_map rhwloc_map_t;

rhwloc_map_t *rhwloc_map_create (const char *xml);
void          rhwloc_map_destroy (rhwloc_map_t *m);

int  rhwloc_map_cores (rhwloc_map_t *m,
                       const char *cores,
                       char **cpus_out,
                       char **mems_out);

char **rhwloc_map_gpu_pci_addrs (rhwloc_map_t *m, const char *gpus);

void rhwloc_map_strv_free (char **strv);

void free (void *);
"""
)

if __name__ == "__main__":
    ffi.emit_c_code("_rhwloc_map.c")
    Path("_rhwloc_map.c").touch()
