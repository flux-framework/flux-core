--[[--------------------------------------------------------------------------
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
 ---------------------------------------------------------------------------]]
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
if time then
    local _clock_gettime = time.clock_gettime
    local CLOCK_REALTIME = time.CLOCK_REALTIME
    rc.clock_gettime = function ()
        local ts = _clock_gettime (CLOCK_REALTIME)
        return ts.tv_sec, ts.tv_nsec
    end
end

return rc
-- vi: ts=4 sw=4 expandtab
