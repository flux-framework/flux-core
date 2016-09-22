"""Global constants for the flux interface"""

from flux._core import ffi, lib
import sys
import re

this_module = sys.modules[__name__]
# Inject enum/define names matching ^FLUX_[A-Z_]+$ into module
all_list = []
p = re.compile("^FLUX_[A-Z_]+")
for k in dir(lib):
    if p.match(k):
        setattr(this_module, k, getattr(lib, k))
        all_list.append(k)

__all__ = all_list

