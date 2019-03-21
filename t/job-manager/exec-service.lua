#!/usr/bin/lua
-------------------------------------------------------------
-- Copyright 2019 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------
--
--  Simple test exec service in-a-script
--  Run as "exec.lua servicename <job duration seconds>"
--
local flux = require 'flux'
local posix = require 'posix'
local f = assert (flux.new())

local service = assert(arg[1], "Required service name argument")
local timeout = arg[2] or 0.
timeout = timeout * 1000.

io.stdout:setvbuf("line")

local function printf (...)
    io.stdout:write (string.format (...))
end

local function die (...)
    io.stderr:write (string.format (...))
    os.exit (1)
end

local jobs = {}

local function job_complete (msg, id, rc)
    assert (msg:respond {
                id = id,
                type = "finish",
                data = {status = rc}
            })
    assert (msg:respond {
                id = id,
                type = "release",
                data = {ranks = "all", final = true}
            })
    jobs[id].timer:remove()
    jobs[id] = nil
    printf ("%s: finish: %d\n", service, id)
end

assert (f:msghandler {
    pattern =   service .. ".start",
    msgtypes =  { flux.MSGTYPE_REQUEST },
    handler = function (f, msg, mh)
        local id = msg.data.id
        jobs[id] = { msg = msg }
        printf ("%s: start: %d\n", service, id)
        assert (msg:respond {id = id, type = "start", data = {}})

        -- Launch job timer:
        jobs[id].timer = assert (f:timer {
            timeout = timeout,
            oneshot = true,
            handler = function () job_complete (msg, id, 0) end
        })

    end
})

assert (f:msghandler {
    pattern = "job-exception",
    msgtypes = { flux.MSGTYPE_EVENT },
    handler = function (f, msg, mh)
        local id = msg.data.id
        printf ("%s: exeception for %d\n", service, id)
        if jobs[id] then
            job_complete (jobs[id].msg, id, 9)
        end
    end
})

printf ("Adding service %s\n", service)
local rc,err = f:rpc ("service.add", { service = service })
if not rc then die ("service.add: %s\n", err) end

assert (f:subscribe ("job-exception"))

local rc,err = f:rpc ("job-manager.exec-hello", { service = service })
if not rc then die ("job-manager.exec-hello: %s\n", err) end

-- Synchronize with test driver by creating a ready key for this service
-- in the kvs:
os.execute ("flux kvs put test.exec-hello."..service.."=1")

f:reactor ()

-- vi: ts=4 sw=4 expandtab
