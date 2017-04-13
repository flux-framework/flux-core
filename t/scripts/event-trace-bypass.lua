#!/usr/bin/env lua
local flux = require 'flux'
local s = arg[1]
local exitevent = arg[2]

function eprintf (...) io.stderr:write (string.format (...)) end

if not s or not exitevent then
    eprintf ([[
Usage: %s TOPIC EXIT-EVENT COMMAND

Subscribe to events matching TOPIC and run COMMAND once subscribe
is guaranteed to be active on the flux broker. If EXIT-EVENT is
not an empty string, then exit the process once an event exactly
matching EXIT-EVENT is received.

This variant calls f:recv_event() thus bypasses the dispatcher
default security policy.
]], arg[0])
    os.exit (1)
end

local cmd = " "
for i = 3, #arg do
    cmd = cmd .. " " .. arg[i]
end

local f,err = flux.new()
f:subscribe (s)
os.execute (cmd .. " &")

repeat
    local msg, tag = f:recv_event()
    print (tag)
until exitevent ~= "" and tag == exitevent
