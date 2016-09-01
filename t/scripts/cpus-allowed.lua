#!/usr/bin/env lua
--
--  Thin frontend to Lua affinity bindings, run cpu_set_t methods against
--   the current cpumask and print the result.
--
local affinity = require 'flux.affinity'
local cpuset = affinity.cpuset

local function die (msg, ...)
    io.stderr:write (string.format (msg, ...))
    os.exit (1)
end

if arg[1] == "--help" or arg[1] == "-h"  then
    print ([[Usage: cpus-allowed [COMMAND] [ARG]...]])
end

local currentmask = assert (affinity.getaffinity ())
if #arg > 0 then
    local method = currentmask[arg[1]] 
    if type (method) ~= "function" then
         die ("cpus-allowed: %s: not a method\n", arg[1])
    end
    local rc = method (currentmask, select (2, unpack (arg)))
    if rc ~= nil then currentmask = rc end
end
print (currentmask)
os.exit (0)
