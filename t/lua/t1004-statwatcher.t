#!/usr/bin/env lua
--
--  Basic flux stat watcher test
--
local test = require 'fluxometer'.init (...)
test:start_session {}

require 'Test.More'

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

-- Test timeout: 5s
f:timer {
    timeout = 5000,
    handler = function () f:reactor_stop_error ("test timed out") end
}

-- Create statwatcher for file "xyzzy"
file = "xyzzy"
local expected = { "Hello", "Goodbye", "" }
local finfo = { line = 0 }

local s, err = f:statwatcher {
    path = file,
    interval = 0.25,
    handler = function (s, st, prev)
        ok (st, "got current stat ok")
        ok (prev, "got prev stat ok")
        if not finfo.fp then
            finfo.fp = io.open (file, "r")
            ok (finfo.fp, "opened file")
        end
        for line in finfo.fp:lines () do
            finfo.line = finfo.line + 1
            is (line, expected [finfo.line], "line "..finfo.line.." matches")
            if finfo.line == #expected then
                f:reactor_stop ()
            end
        end
    end
}
ok (s, "Created statwatcher")

-- In 50ms create the file
f:timer {
    timeout = 50,
    handler = function ()
        local fp = assert (io.open (file, "w"))
        fp:write ("Hello\n")
        fp:close ()
        f:timer {
            timeout = 500,
            handler = function ()
                local fp = assert (io.open (file, "a"))
                fp:write ("Goodbye\n")
                fp:write ("\n")
                fp:close()
            end
        }
    end
}

local r, err = f:reactor ()
ok (r, "reactor exited normally: "..tostring (err))

-- Close fp so we do not carry a file reference into done_testing()
finfo.fp:close ()

done_testing ()

-- vi: ts=4 sw=4 expandtab
