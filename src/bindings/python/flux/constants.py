"""Global constants for the flux interface"""

import sys
import re
from _flux._core import lib

MOD = sys.modules[__name__]
# Inject enum/define names matching ^FLUX_[A-Z_]+$ into module
ALL_LIST = []
PATTERN = re.compile("^FLUX_[A-Z_]+")
for k in dir(lib):
    if PATTERN.match(k):
        setattr(MOD, k, getattr(lib, k))
        ALL_LIST.append(k)

__all__ = ALL_LIST
