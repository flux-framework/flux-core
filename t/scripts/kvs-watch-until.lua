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
--  Exit only if/when all ranks have exited 'unknown' state
--
local usage = [[
Usage: kvs-wait-until [OPTIONS] KEY CODE

Watch kvs KEY until Lua code CODE returns true.
(CODE is supplied key value in variable 'v')
If -t, --timeout is provided, and the timeout expires, then
exit with non-zero exit status.

 -h, --help       Display this message
 -v, --verbose    Print value on each watch callback
 -t, --timeout=T  Wait at most T seconds (before exiting
]]

local getopt = require 'flux.alt_getopt' .get_opts
local timer =  require 'flux.timer'.new()
local f =      require 'flux' .new()

local function printf (...)
    io.stdout:write (string.format (...))
end
local function log_err (...)
    io.stdout:write (string.format (...))
end

local opts, optind = getopt (arg, "hvt:",
                             { verbose = 'v',
                               timeout = 't',
                               help = 'h'
                             }
                            )
if opts.h then print (usage); os.exit (0) end

local key = arg [optind]
local callback = arg [optind+1]

if not key or not callback then
    log_err ("KVS key and callback code required\n")
    print (usage)
    os.exit (1)
end

callback = "return function (v)  return "..callback.." end"
local fn, err = loadstring (callback, "callback")
if not fn then
    log_err ("code compile error: %s", err)
    os.exit (1)
end
local cb = fn ()

local kw, err = f:kvswatcher {
    key = key,
    handler = function (kw, result)
        if opts.v then
            printf ("%4.03fs: %s = %s\n",
                    timer:get0(),
                    key, tostring (result))
        end
        -- Do not pass nil result to callback:
        if result == nil then return end
        local ok, rv = pcall (cb, result)
        if not ok then error (rv) end
        if ok and rv then
            os.exit (0)
        end
    end
}

if opts.t then
    local tw, err = f:timer {
        timeout = opts.t * 1000,
        handler = function (f, to)
               log_err ("%4.03fs: Timeout expired!\n", timer:get0())
               os.exit (1)
        end
    }
end

timer:set ()
f:reactor ()
-- vi: ts=4 sw=4 expandtab
