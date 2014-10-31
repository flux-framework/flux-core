#!/usr/bin/lua

local test = require 'fluxometer'.init (...)
test:start_session{}

local flux = require_ok ('flux')

--
--  Check that first arg is userdata, second arg is nil
--
local function check_userdata_return (name, rc, err)
    is (err, nil, name..": returned error is nil")
    type_ok (rc, 'userdata', name .. " creates new handle")
    return rc
end

--
-- Set key `k' to value `v' and make sure the same type is returned
--  and value matches when we get it back:
--
local function kvsdir_test (d, k, v, comment)
    local msg = string.format ("kvsdir: %s: ", comment)
    local t = type (v)
    d[k] = v
    d:commit ()
    local r = d[k]
    is (type(r), t, msg .. "got back type " .. t)
    if t == 'table' then
        is_deeply (r, v, msg .. "table values match")
    else
        is (r, v, msg .. "values match")
    end
end

local f = check_userdata_return ("flux.new", flux.new())
local dir = check_userdata_return ("f:kvsdir", f:kvsdir ())
local key = "testkey"
kvsdir_test (dir, key, "foo", "string")
kvsdir_test (dir, key, 42,    "numeric value")
kvsdir_test (dir, key, true,  "true")
kvsdir_test (dir, key, false, "false")
kvsdir_test (dir, key, { x = 1, y = 2 }, "a table")
kvsdir_test (dir, key, { x = { "a", "deep", "table" } }, "a deeper table")
kvsdir_test (dir, key, { 1, 2, 3, 4, 5, 6 }, "an array")


-- unlink testing
dir.testkey = "foo"
kvsdir_test (dir, 'testkey', "foo", "unlink: set known value for key")
dir.testkey = nil
kvsdir_test (dir, 'testkey', nil, "unlink: key is now nil")


-- Iterator testing
local keys = { "a", "b", "c", "d", "e" }
for _,k in pairs (keys) do
    dir ["x."..k] = "foo"
end
dir:commit()
local d = check_userdata_return ("f:kvsdir('x')", f:kvsdir ("x"))
local n = 0
for key in d:keys() do
    n = n + 1
    is (d[key], "foo", "key x."..key.." is correct")
end
is (n, #keys, "keys() iterator returned correct number of keys")

-- KVS watcher creation
local data = { key = "testkey", value = "testvalue" }

local count = 0
dir [data.key] = nil -- Unlink current key

local kw = f:kvswatcher {
    key = data.key,
    handler = function (kw, result)
        type_ok (kw, 'userdata', "kvs watch handle returned to handler")
        count = count + 1
        if count == 1 then
            is (result, nil, "kvswatcher: initial callback has result == nil")
        else
            is (result, data.value, "kvswatcher: 2nd callback has correct result")
            return f:reactor_stop()
        end
    end
}

os.execute (string.format ("flux kvs put %s=%s", data.key, data.value))
local r = f:reactor()

is (r, 0, "reactor exited normally")

done_testing ()

-- vi: ts=4 sw=4 expandtab
