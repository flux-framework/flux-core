#!/usr/bin/lua
--
--  Basic flux/lua signal handler interface testing
--
local test = require 'fluxometer'.init (...)
test:start_session {}

require 'Test.More'

local flux = require_ok ('flux')
local posix = require_ok ('posix')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

success = false
local sigh, err = f:sighandler {
    sigmask = {posix.SIGINT},
    handler = function (f, s, sig)
        is (s.test, "foo", "Able to retrieve private key in signal handler table")
        is (sig, posix.SIGINT, "Got SIGINT")
        f:reactor_stop()
    end
}
type_ok (sigh, 'userdata', "created signal handler")
is (err, nil, "error from signal handler creation is nil")

sigh.test = "foo"
is (sigh.test, "foo", "Successfully set key in signal handler table")

local to, err = f:timer {
    timeout = 500,
    handler = function (f, to)
        fail ("Failed to get signal withn 500ms")
        return -1
    end
}
type_ok (to, 'userdata', "created timeout handler")
is (err, nil, "error from timer creation is nil")

local pid = posix.getpid ()
os.execute ('sleep .2 && kill -INT '..pid.pid)

local r, err = f:reactor()
isnt (r, -1, "Return from reactor, rc >= 0")
is (err, nil, "error is nil")

is (sigh:remove(), nil, "Removing signal handler works")

done_testing ()

-- vi: ts=4 sw=4 expandtab
