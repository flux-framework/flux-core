#!/usr/bin/lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local test = require 'fluxometer'.init (...)
test:start_session { size=1 }

plan (7)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

--
--  Use 'ping' packet to test rpc
--
local packet = { seq = "1", pad = "xxxxxx" }
local msg, err = f:rpc ("live.ping", packet)
type_ok (msg, 'table', "rpc: return type is table")
is (err, nil, "rpc: err is nil")
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")

done_testing ()
