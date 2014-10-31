#!/usr/bin/lua
--
--  Basic flux reactor timeout handler testing
--
local test = require 'fluxometer'.init (...)
test:start_session{}

local flux = require_ok ('flux')
local alarm = require_ok ('lalarm')

-- Ensure we complete in 2s
local timeout = false
alarm (2, function() timeout = true; diag("in alarm handler") end)

local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

local count = 0
local to, err = f:timer {
    timeout = 250,
    handler = function (f, to)
	    type_ok (to, 'userdata', 'got userdata in callback')
	    count = count + 1
    end
}
type_ok (to, 'userdata', "created timeout handler")
is (err, nil, "error from timer create is nil")
is (to.timeout, 250, 'timeout is 500ms')
type_ok (to.id, 'number', 'new timeout id is '..to.id)

local r = f:reactor()
isnt(r, -1, "returned from reactor with return code "..tostring (r))
is (count, 1, 'timer fired exactly once')

is (timeout, false,
    "oneshot timer loop does not need to be interrupted (issue 84)")


-- Remove old timer:
to:remove()
to = nil

local count = 0
local t, err = f:timer {
    timeout = 100,
    oneshot = false,
    handler = function (f, to)
	    count = count + 1
        if count == 3 then to:remove() end
    end
}
type_ok (t, 'userdata', "created timeout handler")
is (err, nil, "error from timer create is nil")
is (t.timeout, 100, 'timeout is 100ms')
is (t.oneshot, false, 'oneshot is false')
type_ok (t.id, 'number', 'new timeout id is '..t.id)


local r = f:reactor()
isnt(r, -1, "returned from reactor with return code "..tostring (r))
is (count, 3, 'timer should fire 3 times')

done_testing ()

-- vi: ts=4 sw=4 expandtab
