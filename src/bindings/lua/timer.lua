-- Simple timer implementation
--  Returns time in seconds since timer was created, or since last get/set
--
local T = {}
T.__index = T
local clock_gettime = require 'posix'.clock_gettime

local getsec = function ()
    local posix = require 'posix'
    local s,ns = posix.clock_gettime()
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
