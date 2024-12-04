##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

from flux.modprobe import task


def setup(context):
    context.remove_modules()


@task(
    "content-flush",
    ranks="0",
    after=["kvs"],
    before=["content-backing"],
    needs=["kvs"],
)
def content_flush(context):
    context.rpc("content.flush").get()
