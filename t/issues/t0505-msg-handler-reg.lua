#!/usr/bin/env lua

-------------------------------------------------------------
-- Copyright 2014 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

--
-- Issue #505: Ensure msghandler can be installed from within an event handler.
--
local flux = require 'flux'
local f,err = flux.new ()
if not f then error (err) end


local function die (...)
    io.stderr:write (string.format (...))
    os.exit (1)
end

local event_count = 0
f:msghandler {
    pattern = "testevent",
    msgtypes = { flux.MSGTYPE_EVENT },
    handler = function (f, msg, mh)
        event_count = event_count + 1
        if (event_count > 1) then
            die ("Fatal: got more than 1 expected event\n")
        end
        f:send ("kvs.ping", { seq = "1", pad = "" })
        f:msghandler {
            pattern = "kvs.ping",
            msgtypes = { flux.MSGTYPE_RESPONSE },
            handler = function (f, msg, mh) f:reactor_stop () end
        }
    end
}

f:subscribe ("testevent")
f:sendevent ("testevent")
f:reactor ()
if (event_count ~= 1) then
    die ("Expected event_count=0 got %d\n", event_count)
end

-- vi: ts=4 sw=4 expandtab
