#!/usr/bin/env lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local test = require 'fluxometer'.init (...)
test:start_session { size = 2 }

local flux = require_ok ('flux')

ok (flux.MSGTYPE_RESPONSE, "have MSGTYPE_RESPONSE")
ok (flux.MSGTYPE_REQUEST,  "have MSGTYPE_REQUEST")
ok (flux.MSGTYPE_EVENT,    "have MSGTYPE_EVENT")
ok (flux.MSGTYPE_ANY,      "have MSGTYPE_ANY")
ok (flux.NODEID_UPSTREAM,  "have NODEID_ANY")
ok (flux.NODEID_ANY,       "have NODEID_UPSTREAM")

local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

local count = 0

local mh, err = f:msghandler {
    pattern = "*.ping",
    msgtypes = { flux.MSGTYPE_RESPONSE, flux.MSGTYPE_REQUEST },

    handler = function (f, zmsg, mh)
        local resp = zmsg.data
        is (zmsg.errnum, 0, "ping: no error in response msg")
        is (zmsg.tag, "kvs.ping", "ping: got correct tag")
        is (resp.seq, "1", "ping: got first sequence number")
        is (resp.pad, "TestPAD", "ping: got correct pad")
        mh:remove()
    end
}
type_ok (mh, 'userdata', "created message handler")

local rc, err = f:send ("kvs.ping", { seq = "1", pad = "TestPAD" })
ok (rc, "ping: rpc:send successful")
is (err, nil, "ping: rcp:send no error")

local r = f:reactor()

isnt(r, -1, "returned from reactor with return code "..tostring (r))


--
local mh, err = f:msghandler {
    pattern = "*.ping",
    msgtypes = { flux.MSGTYPE_RESPONSE, flux.MSGTYPE_REQUEST },

    handler = function (f, zmsg, mh)
        local resp = zmsg.data
        is (resp.pad, "P2", "ping: got updated pad")
        return -1
    end
}
type_ok (mh, 'userdata', "created another message handler")


local rc, err = f:send ("kvs.ping", { seq = "1", pad = "P2"})
ok (rc, "ping: rpc:send successful")
is (err, nil, "ping: rcp:send no error")

local r,err = f:reactor()
is (r, nil, "return with -1 from msghandler aborts reactor with nil,err")

todo ("Need to fix error on return from msghandler", 1)
is (err, "", "err is '"..err.."'")

mh:remove()


--
--

-- Do nothing in top level msghandler:

local second_msghandler = false

local mh2, err = f:msghandler {
    pattern = "*.ping",
    msgtypes = { flux.MSGTYPE_RESPONSE, flux.MSGTYPE_REQUEST },

    handler = function (f, zmsg, mh)
        second_msghandler = true
        return -1
    end
}

-- Do nothing in top level msghandler:

local first_msghandler = false
mh, err = f:msghandler {
    pattern = "*.ping",
    handler = function (f, zmsg, mh)
        first_msghandler = true
        return -1
    end
}



type_ok (mh, 'userdata', "created another message handler")

local rc, err = f:send ("kvs.ping", { seq = "1", pad = "P2"})
ok (rc, "ping: rpc:send successful")
is (err, nil, "ping: rcp:send no error")
local r, err = f:reactor()
is (r, nil, "Exited from reactor as expected")
is (first_msghandler, true, "Message passed to first msghadler")

todo ("All zmsgs consumed by first handler in lua code")
is (second_msghandler, true, "Message passed to second msghadler")

-- Test for setting reason on reactor_stop_error()
local t, err = f:timer {
    timeout = 1,
    handler = function (f, to) return f:reactor_stop_error ("because") end
}

local r, err = f:reactor()
is (r, nil, "reactor returned nil")
is (err, "because", "got expected reason from reactor_stop_error()")

done_testing ()

-- vi: ts=4 sw=4 expandtab
