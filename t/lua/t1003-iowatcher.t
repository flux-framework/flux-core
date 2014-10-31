#!/usr/bin/lua
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
    handler = function (iow, line)
        if not line then f:reactor_stop() end
        table.insert (data, line)
    end
}
type_ok (iow, 'userdata', "succesfully create iowatcher")
is (err, nil, "error is nil")

os.execute 'flux zio --force --key=iowatcher.test --run printf "hello\nworld"'

local r, err = f:reactor()
isnt (r, -1, "Return from reactor, rc >= 0")
is (err, nil, "error is nil")

is (data[1], 'hello\n', "first line is hello")
is (data[2], 'world', "second line is world")

done_testing ()

-- vi: ts=4 sw=4 expandtab
