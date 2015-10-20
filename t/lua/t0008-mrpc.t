#!/usr/bin/lua
--
--  Basic flux mrpc testing using mecho service
--
local test = require 'fluxometer'.init (...)
test:start_session { size=4 }

plan ('no_plan')

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

--
--  Use 'mecho' packet to test rpc
--
local inarg = { test = "Hello, World" }
local m, err = f:mrpc ("0-3", inarg)
ok (m, "mrpc created")

-- Force commit so we can check inarg
f:kvs_commit ()

-- Check inarg
is_deeply (m.inarg, inarg, "inarg set by f:mrpc")

-- Check that we can put new inarg
local inarg2 = { foo = "bar" }
m.inarg = inarg2
f:kvs_commit ()
is_deeply (m.inarg, inarg2, "inarg set by newindex")

m.inarg = inarg

local out, err = m ("mecho")
ok (out, "mrpc returned "..tostring (out))

for i, arg in m.out:next() do
    is_deeply (arg, inarg, i..": outarg matches")
end

-- Also try direct index into outargs
local outargs = m.out
ok (outargs, "index outargs")
is_deeply (outargs[0], inarg, "direct index into outargs works")


done_testing ()
