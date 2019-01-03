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
-- Simple timer implementation
--  Returns time in seconds since timer was created, or since last get/set
--
local T = {}
T.__index = T
local clock_gettime = require 'flux.posix'.clock_gettime

-- Use version of clock_gettime from posix.time if it exists, o/w fallback to
--  version from posix/deprecated.lua
--

local getsec = function ()
    local s,ns = clock_gettime()
    return (s + (ns / 1000000000))
end

function T.new()
    local new = { s0 = getsec () }
    return setmetatable (new, T)
end

function T.set (self)
   self.t0 = getsec()
   return self.t0
end

function T.get (self)
    local x = self.t0
    return (self:set() - (x or 0))
end

function T.get0 (self)
    return (getsec() - self.s0)
end

return T
-- vi: ts=4 sw=4 expandtab
