##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################


def setup(context):
    context.setopt("content", "blob-size-limit=1048576")
    context.load_modules(["content", "content-sqlite", "kvs", "kvs-watch", "heartbeat"])
