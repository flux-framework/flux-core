from pathlib import Path
from cffi import FFI
import os

# Ensure paths are in _flux
here = os.path.abspath(os.path.dirname(__file__))

source = os.environ.get("FLUX_SECURITY_SOURCE")
includes = os.environ.get("FLUX_SECURITY_INCLUDE")

preproc_file = os.path.join(here, "_security_preproc.h")
core_c_file = os.path.join(here, "_security.c")

ffi = FFI()


ffi.set_source(
    "_flux._security",
    """
#include <flux/security/sign.h>
            """,
    libraries=[
        "flux-security",
        "security",
        "munge",
        "json-glib-1.0",
        "util",
        "tomlc99",
    ],
    library_dirs=[
        "/usr/local/lib",
        "/usr/lib/x86_64-linux-gnu",
        f"{source}/src/libutil/.libs",
        f"{source}/src/lib",
        f"{source}/src/lib/.libs",
        f"{source}/src/libtomlc99/.libs",
    ],
    include_dirs=[
        "/usr/local/include",
        f"{source}/src/lib",
        f"{source}/src/libutil",
        f"{source}/src/libtomlc99",
        includes,
    ],
    extra_compile_args=[
        f"-L{source}/src/lib",
        "-L/usr/lib/x86_64-linux-gnu",
        f"-L{source}/src/libutil/.libs",
        f"-L{source}/src/lib/.libs",
        f"-L{source}/src/libtomlc99/.libs",
    ],
)

cdefs = """
typedef int... ptrdiff_t;
typedef int... pid_t;
typedef ... va_list;
void free(void *ptr);

    """

with open(preproc_file) as h:
    cdefs = cdefs + h.read()

ffi.cdef(cdefs)
ffi.emit_c_code(core_c_file)
# ensure target mtime is updated
Path(core_c_file).touch()
ffi.compile(verbose=True)
