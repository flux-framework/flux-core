#!/usr/bin/env lua
--
--  Basic flux reactor testing using ping interface to kvs
--
local test = require 'fluxometer'.init (...)
test:start_session {}

require 'Test.More'

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

local dir = f:kvsdir()
dir['iowatcher'] = nil
dir:commit()

local data = {}
local iow, err = f:iowatcher {
    key = "iowatcher.test.stdout",
    handler = function (iow, lines)
        if not lines then f:reactor_stop(); return  end
        -- Can get multiple lines per callback, by the by
        lines:gsub ('([^\n]+\n?)', function (s)
            table.insert (data, s)
        end)
    end
}
type_ok (iow, 'userdata', "succesfully create iowatcher")
is (err, nil, "error is nil")
type_ok (iow.kz, 'userdata', "iowatcher kz available as index")

os.execute ('printf "hello\nworld" | ' .. test.top_builddir .. '/t/kz/kzutil --copy - iowatcher.test.stdout')

f:timer {
    timeout = 250,
    hander = function () f:reactor_stop_error () end
}
local r, err = f:reactor()
isnt (r, -1, "Return from reactor, rc >= 0")
is (err, nil, "error is nil")

is (data[1], 'hello\n', "first line is hello")
is (data[2], 'world', "second line is world")

done_testing ()

-- vi: ts=4 sw=4 expandtab
