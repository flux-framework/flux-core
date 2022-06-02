###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# idset 2 bitmask tool

import sys
import flux.idset as idset

if len(sys.argv) != 2:
    print("Usage: idset2bitmask <idset>")
    sys.exit(1)

ids = idset.decode(sys.argv[1])
n = 0
for i in ids:
    n |= 0x1 << i
print(f"0x{n:x}")

# vi: ts=4 sw=4 expandtab
