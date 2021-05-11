###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# startctl - tell flux-start to do things
#
# Usage: flux start -s1 flux python startctl.py

import os
import flux


def status(h):
    print(h.rpc("start.status").get_str())


def main():
    h = flux.Flux(os.environ.get("FLUX_START_URI"))
    status(h)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
