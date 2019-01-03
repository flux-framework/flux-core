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
 -- flux-lua.posix: wrapper to check for existing of system 'posix'
 --  module and return user friendly error instead lua stack dump
 --
local status, rc = pcall (require, 'posix')
if not status then
    io.stderr:write ("Required Lua 'posix' module not found."..
                    " Please check LUA_PATH or install package\n")
    os.exit (1)
end

-- Use non-deprecated version of clock_gettime if available:
local status, time = pcall (require, 'posix.time')
if time and time.clock_gettime then
    local _clock_gettime = time.clock_gettime
    local CLOCK_REALTIME = time.CLOCK_REALTIME
    rc.clock_gettime = function ()
        local ts = _clock_gettime (CLOCK_REALTIME)
        return ts.tv_sec, ts.tv_nsec
    end
end

return rc
-- vi: ts=4 sw=4 expandtab
