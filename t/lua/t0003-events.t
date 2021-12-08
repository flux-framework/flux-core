#!/usr/bin/env lua
--
--  Basic flux event testing
--
local test = require 'fluxometer'.init (...)
test:start_session {}

local fmt = string.format

plan (28)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

local rc, err = f:subscribe ("testevent.")
isnt (rc, nil, "subscribe: return code != nil")
is (err, nil, "subscribe: error is nil")

local rc, err = f:unsubscribe ("notmytopic")
is (rc, nil, "unsubscribe: return code == nil")
is (err, "No such file or directory",
    "unsubscribe: error is No such file or directory")

local rc, err = f:sendevent ({ test = "xxx" }, "testevent.1")
isnt (rc, nil, "sendevent: return code != nil")
is (err, nil, "sendevent: error is nil")

local msg, tag = f:recv_event ()
is (tag, 'testevent.1', "recv_event: got expected event tag")
type_ok (msg, 'table', "recv_event: got msg as a table")
is (msg.test, "xxx", "recv_event: got payload intact")


-- sendevent takes string.format args...
local rc, err = f:sendevent ({ test = "yyy"}, "testevent.%d", 2)
isnt (rc, nil, "sendevent: return code != nil")
is (err, nil, "sendevent: error is nil")

local msg, tag = f:recv_event ()
is (tag, 'testevent.2', "recv_event: got expected event tag")
type_ok (msg, 'table', "recv_event: got msg as a table")
is (msg.test, "yyy", "recv_event: got payload intact")


-- sendevent with empty payload...
local rc, err = f:sendevent ("testevent.%d", 2)
isnt (rc, nil, "sendevent: return code != nil")
is (err, nil, "sendevent: error is nil")

local msg, tag = f:recv_event ()
is (tag, 'testevent.2', "recv_event: got expected event tag")
type_ok (msg, 'table', "recv_event: got msg as a table")
is_deeply (msg, {}, "recv_event: got empty payload as expected")


-- poke at event.publish service
-- good request, no payload
local request = { topic = "foo", flags = 0 }
local response, err = f:rpc ("event.publish", request);
is (err, nil, "event.publish: works without payload")

-- good request, with raw payload
local request = { topic = "foo", flags = 0, payload = "aGVsbG8gd29ybGQ=" }
local response, err = f:rpc ("event.publish", request);
is (err, nil, "event.publish: works with payload")

-- good request, with JSON "{}\0"
local request = { topic = "foo", flags = 0, payload = "e30A" }
local response, err = f:rpc ("event.publish", request);
is (err, nil, "event.publish: works with json payload")

-- flags missing from request
local request = { topic = "foo" }
local response, err = f:rpc ("event.publish", request);
is (err, "Protocol error", "event.publish: no flags, fails with EPROTO")

-- mangled base64 payload
local request = { topic = "foo", flags = 0, payload = "aGVsbG8gd29ybGQ%" }
local response, err = f:rpc ("event.publish", request);
is (err, "Protocol error", "event.publish: bad base64, fails with EPROTO")

-- good request, mangled JSON payload "{\0"
local request = { topic = "foo", flags = 4, payload = "ewA=" }
local response, err = f:rpc ("event.publish", request);
is (err, "Protocol error", "event.publish: bad json payload, fails with EPROTO")

done_testing ()

-- vi: ts=4 sw=4 expandtab
