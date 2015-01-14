#!/usr/bin/lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local t = require 'fluxometer'.init (...)
t:start_session { size = 2}
t:say ("starting send/recv tests")

plan (13)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

--
--  Use 'ping' packet to test send/recv:
--

local packet = { seq = "1", pad = "xxxxxx" }
local rc, err = f:send ("live.ping", packet)
is (rc, 1, "send: rc is 1")
is (err, nil, "send: err is nil")


local msg, tag = f:recv ()
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")
is (tag, "live.ping", "recv: got expected tag on ping response")

for k,v in pairs(msg) do note ("msg."..k.."="..v) end

--
--  Test f:send() with rank argument
--
local rc, err = f:send ("live.ping", packet, 0)
is (rc, 1, "send to rank 0: rc is 1")
is (err, nil, "send to rank 0: err is nil")

local msg, tag = f:recv ()
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")
is (tag, "live.ping", "recv: got expected tag on ping response")

done_testing ()

-- vi: ts=4 sw=4 expandtab
