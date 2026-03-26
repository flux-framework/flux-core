#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
"""rreq-reader: parse a JSON jobspec from stdin, print ResourceRequest fields.

Output (one line):
  nnodes=N nslots=M slot_size=P slot_gpus=R exclusive=T/F duration=D.D

Exit 1 and print error to stderr on failure.
"""

import json
import sys

from flux.resource.Rv1Pool import ResourceRequest


def main():
    try:
        jobspec = json.load(sys.stdin)
        rr = ResourceRequest.from_jobspec(jobspec)
    except (ValueError, KeyError) as e:
        print(f"rreq-reader: {e}", file=sys.stderr)
        sys.exit(1)

    print(
        f"nnodes={rr.nnodes} nslots={rr.nslots} "
        f"slot_size={rr.slot_size} slot_gpus={rr.gpu_per_slot} "
        f"exclusive={str(rr.exclusive).lower()} "
        f"duration={rr.duration:.1f}"
    )


if __name__ == "__main__":
    main()
