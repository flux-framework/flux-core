#!/usr/bin/env lua
--
--  Basic seq testing
--
local test = require 'fluxometer'.init (...)
test:start_session { size=1 }

plan (54)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

is (f.rank, 0, "running on rank 0")

--
--  fetch and create
--
local request = { name = "foo",
		  create = true,
                  preincrement = 0,
                  postincrement = 0 }
local r, err = f:rpc ("seq.fetch", request)
type_ok (r, 'table', "seq.create: returned table")
is (err, nil, "seq.create: err is nil")
is (r.name, "foo", "seq.create: name is correct")
is (r.value, 0, "seq.create: initial value is 0")
is (r.created, true, "seq.create: created = true")

local r, err = f:rpc ("seq.fetch", request)
type_ok (r, 'table', "seq.create: returned table")
is (err, nil, "seq.create: err is nil")
is (r.name, "foo", "seq.create: name is correct")
is (r.value, 0, "seq.create: initial value is 0")
is (r.created, nil, "seq.create: created = false")

--
--  seq.destroy
--
local r, err = f:rpc ("seq.destroy", { name = "foo" })
type_ok (r, 'table', "seq.destroy: returned table")
is (err, nil, "seq.destroy: err is nil")
is (r.name, "foo", "seq.destroy: name is correct")
is (r.destroyed, true, "seq.destroy: got true return code")

local r, err = f:rpc ("seq.destroy", { name = "foo" })
is (r, nil, "seq.destroy: nil return value")
is (err, "No such file or directory", "seq.destroy: ENOENT")

--
--  seq.fetch
--
local request = { name = "xxx",
		  create = false,
                  preincrement = 0,
                  postincrement = 0 }
local r, err = f:rpc ("seq.fetch", request)
is (r, nil, "seq.fetch: nil return value")
is (err, "No such file or directory", "seq.fetch: error with ENOENT")

-- fetch and create with initial value
local request = { name = "aaa",
		  create = true,
                  preincrement = 9,
                  postincrement = 0 }
local r, err = f:rpc ("seq.fetch", request)
type_ok (r, 'table', "seq.fetch: returned table")
is (err, nil, "seq.fetch: err is nil")
is (r.name, "aaa", "seq.fetch: name is correct")
is (r.value, 9, "seq.fetch (new): got 9 as initial value")
is (r.created, true, "seq.fetch (new): got created")

-- increment and fetch
request.preincrement = 1
request.postincrement = 0
request.create = false
local r, err = f:rpc ("seq.fetch", request)
is (r.value, 10, "seq.fetch (increment)")
is (r.created, nil, "seq.fetch (increment): r.created is false")

local r, err = f:rpc ("seq.fetch", request)
is (r.value, 11, "seq.fetch (increment)")
is (r.created, nil, "seq.fetch (increment): r.created is false")

local r, err = f:rpc ("seq.fetch", request)
is (r.value, 12, "seq.fetch (increment)")
is (r.created, nil, "seq.fetch (increment): r.created is false")

-- decrement and fetch
request.preincrement = -1
local r, err = f:rpc ("seq.fetch", request)
is (err, nil, "seq.fetch (decrement) nil err")
is (r.value, 11, "seq.fetch (decrement)")
is (r.created, nil, "seq.fetch (decrement): r.created is false")

-- fetch and decrement
request.preincrement = 0
request.postincrement = -1
local r, err = f:rpc ("seq.fetch", request)
is (err, nil, "seq.fetch (decrement) nil err")
is (r.value, 11, "seq.fetch (decrement)")

local r, err = f:rpc ("seq.fetch", request)
is (err, nil, "seq.fetch (decrement) nil err")
is (r.value, 10, "seq.fetch (decrement)")

-- fetch
request.preincrement = 0
request.postincrement = 0
local r, err = f:rpc ("seq.fetch", request)
is (err, nil, "seq.fetch (after increment) nil err")
is (r.value, 9, "seq.fetch (after increment)")

-- set
local r, err = f:rpc ("seq.set", { name = "aaa", value = 0 })
is (err, nil, "seq.set: nil err")
is (r.name, "aaa", "seq.set: got expected name")
is (r.value, 0, "seq.set: new value = 0")

local r, err = f:rpc ("seq.set", { name = "aaa", value = 0, oldvalue = 11 })
is (err, "Resource temporarily unavailable", "seq.set: with oldvalue: error")

local r, err = f:rpc ("seq.set", { name = "aaa", value = 22, oldvalue = 0 })
is (err, nil, "seq.set: with correct oldvalue: no error")
is (r.name, "aaa", "seq.set: got expected name")
is (r.value, 22, "seq.set: new value = 22")

local r, err = f:rpc ("seq.set", { name = "aaa", value = 0, oldvalue = 22 })
is (r.value, 0, "seq.set: swap back to zero")

-- negative values work
request.preincrement = -1
local r, err = f:rpc ("seq.fetch", request)
is (err, nil, "seq.fetch (negative) nil err")
is (r.value, -1, "seq.fetch (after decrement)")

-- missing JSON members return errors
local r, err = f:rpc ("seq.fetch", { name = "bbb", create = true, preincrement = 0 })
is (r, nil, "seq.fetch: missing members return error")
is (err, "Protocol error", "seq.fetch: missing members return Protocol error")

done_testing ()
