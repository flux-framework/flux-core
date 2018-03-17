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
