"""Global constants for the flux interface"""

from flux._core import lib
import sys
import re

MOD = sys.modules[__name__]
# Inject enum/define names matching ^FLUX_[A-Z_]+$ into module
ALL_LIST = []
PATTERN = re.compile("^FLUX_[A-Z_]+")
for k in dir(lib):
    if PATTERN.match(k):
        setattr(MOD, k, getattr(lib, k))
        ALL_LIST.append(k)

__all__ = ALL_LIST

