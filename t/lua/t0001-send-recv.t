#!/usr/bin/env lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local t = require 'fluxometer'.init (...)
t:start_session { size = 2}

plan (34)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

--
--  Use 'ping' packet to test send/recv:
--

local packet = { seq = "1", pad = "xxxxxx" }
local matchtag, err = f:send ("cmb.ping", packet)
isnt (rc, 0, "send: rc is not 0")
is (err, nil, "send: err is nil")


local msg, tag = f:recv ()
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")
is (tag, "cmb.ping", "recv: got expected tag on ping response")

for k,v in pairs(msg) do note ("msg."..k.."="..v) end

--
--  Test f:send() with rank argument
--
local rc, err = f:send ("cmb.ping", packet, 0)
isnt (rc, 0, "send to rank 0: rc is not nil or zero")
is (err, nil, "send to rank 0: err is nil")

local msg, tag = f:recv ()
is (msg.seq, "1", "recv: got expected ping sequence")
is (msg.pad, "xxxxxx", "recv: got expected ping pad")
is (tag, "cmb.ping", "recv: got expected tag on ping response")


---
---  Test send with recvmsg()
---
local matchtag, err = f:send ("cmb.ping", packet, 1)
isnt (rc, 0, "send to rank 1: rc is not nil or zero")
isnt (rc, nil, "send to rank 1: rc is not nil or zero")
is (err, nil, "send to rank 1: err is nil")

local msg = f:recvmsg ()
ok (msg, "msg is non-nil")
is (msg.matchtag, matchtag, "recvmsg: matchtag matches (".. matchtag..")")
is (msg.errnum, 0, "recvmsg: message errnum is 0")
is (msg.data.seq, "1", "recv: got expected ping sequence")
is (msg.data.pad, "xxxxxx", "recv: got expected ping pad")
is (msg.tag, "cmb.ping", "recv: got expected tag on ping response")


f, err = flux.new ("loop://")
type_ok (f, 'userdata', "create loop connector")
is (err, nil, "error is nil")

local rc, err = f:send ("test", {foo = "bar"}, 0)
isnt (rc, 0, "loop: send over loop connector works")
is (err, nil, "loop: err is nil")

local msg, err = f:recvmsg ()
ok (msg, "loop: recvmsg: msg is non-nil")
ok (msg.type, "request", "loop: recvmsg: msg.type is correct")
is (msg.tag, "test", "loop: got expected message tag")
is (msg.data.foo, "bar", "loop: recvd message with expected payload")

msg:respond { bar = "baz" }

local msg, err = f:recvmsg ()
ok (msg, "loop: got response")
ok (msg.type, "response", "loop: recvmsg response")
ok (msg.tag, "test", "response: msg.tag is correct")
ok (msg.data.bar, "baz", "response: msg.data payload is correct")

done_testing ()

-- vi: ts=4 sw=4 expandtab
