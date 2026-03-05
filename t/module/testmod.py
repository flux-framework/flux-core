##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

# testmod.py - minimal Python broker module for testing flux-module-python-exec

from flux.brokermod import BrokerModule, event_handler, request_handler


class TestMod(BrokerModule):

    @request_handler("hello")
    def hello(self, msg):
        self.handle.respond(msg, {"name": self.name})

    @request_handler("die")
    def die(self, msg):
        raise RuntimeError("die handler raised per test request")

    @event_handler("panic")
    def panic(self, msg):
        self.stop_error()


def mod_main(h, *args):
    for arg in args:
        if arg == "--init-failure":
            raise RuntimeError("init failure per test request")
    TestMod(h, *args).run()


# vi: ts=4 sw=4 expandtab
