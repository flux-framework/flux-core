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
--  Get a json key from kvs and execute Lua CODE on result
--
local usage = [[
Usage: kvs-get-ex KEY CODE

Get kvs key KEY and execute Lua CODE where x == result.
]]

local f =      require 'flux' .new()

local function printf (...)
    io.stdout:write (string.format (...))
end
local function die (...)
    io.stderr:write (string.format (...))
    os.exit (1)
end

local optind = 1
local key = arg [optind]
local callback = arg [optind+1]

if not key or not callback then
    die ("Failed to get KEY or CODE\n%s", usage)
end

callback = "return function (x)  return "..callback.." end"
local fn, err = loadstring (callback, "callback")
if not fn then
    die ("code compile error: %s", err)
end
local cb = fn ()

local result, err = f:kvs_get (key)
if not result then
    die ("kvs_get (%s): %s\n", key, err)
end

local ok, rv = pcall (cb, result)
if not ok then error (rv) end
if not ok or rv == false then
    os.exit (1)
end

-- vi: ts=4 sw=4 expandtab
