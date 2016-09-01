#!/usr/bin/env lua
--
--  Basic flux fdwatcher test
--
local test = require 'fluxometer'.init (...)
test:start_session {}

require 'Test.More'

local data = { "Hello", "Goodbye" }

local flux = require_ok ('flux')
local posix = require_ok ('flux.posix')

if not posix.pipe then
    skip_all ("luaposix too old, no pipe(2) call found")
end

local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

local r, w = posix.pipe ()
ok (r and w, "posix.pipe ()")

local i = 1
f:timer {
    timeout = 100,
    oneshot = false,
    handler = function (f, t) 
        if not data[i] then
            diag ("closing pipe")
            posix.close (w)
            t:remove()
            return
        end
        diag ("writing "..#data[i].. " bytes to pipe")
        posix.write (w, data[i])
        i = i + 1
    end
}

local results = {}
f:fdwatcher {
    fd = r,
    handler = function (fw)
        local s, err = posix.read (r, 100)
        ok (s, "read ".. #s .. " bytes")
        if s == "" then
            fw:remove()
            return f:reactor_stop()
        end
        table.insert (results, s)
    end
}

-- test timeout
f:timer {
    timeout = 1000,
    handler = function () f:reactor_stop_error () end
}

local r, err = f:reactor()
isnt (r, -1, "Return from reactor, rc >= 0")
is (err, nil, "error is nil")
is_deeply (results, data, "Results match data")

done_testing ()

-- vi: ts=4 sw=4 expandtab
