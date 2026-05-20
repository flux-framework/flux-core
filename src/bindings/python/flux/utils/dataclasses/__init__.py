###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Re-export the full public API of the stdlib dataclasses module (minus
# KW_ONLY, which was added in 3.10 and is absent from this backport).
from flux.utils.dataclasses.dataclasses import (
    FrozenInstanceError,
    InitVar,
    Field,
    MISSING,
    field,
    fields,
    dataclass,
    asdict,
    astuple,
    make_dataclass,
    replace,
    is_dataclass,
)
