#!/usr/bin/env lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local test = require 'fluxometer'.init (...)
test:start_session { size=2 }

plan (18)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

is (f.rank, 0, "running on rank 0")
is (f.size, 2, "session size is 2")

--
--  Use 'ping' packet to test rpc
--
local packet = { seq = "1", pad = "xxxxxx" }
local msg, err = f:rpc ("broker.ping", packet)
type_ok (msg, 'table', "rpc: return type is table")
is (err, nil, "rpc: err is nil")
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")

--
--  Send invalid 'ping' packet to test response
--
local packet = { 1, 2, 3 }  -- Force encoding to be 'array'
local msg, err = f:rpc ("broker.ping", packet)
is (msg, nil, "rpc: invalid packet: nil response indicates error")
is (err, "Invalid argument", "rpc: invalid packet: err is 'Invalid argument'")

--
-- 'ping' to specific rank
--
local packet = { seq = "1", pad = "xxxxxx" }
local msg, err = f:rpc ("broker.ping", packet, 1)
type_ok (msg, 'table', "rpc: return type is table")
is (err, nil, "rpc: err is nil")
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")
is (msg.errnum, nil, "recv: errnum is zero")

--
-- 'ping' to non-existent rank, check error return
--
note ("ping to non-existent rank")
local packet = { seq = "1", pad = "xxxxxx" }
local msg, err = f:rpc ("broker.ping", packet, 99)
is (msg, nil, "rpc: nil return indicates error")
ok (err == "No route to host" or err == "Host is unreachable", "rpc: err is 'No route to host' or 'Host is unreachable'")

done_testing ()
