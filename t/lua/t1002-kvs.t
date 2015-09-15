#!/usr/bin/lua

local test = require 'fluxometer'.init (...)
test:start_session{ size=4 }

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
    local t2 = type (r)
    is (t2, t, msg .. "got back type " .. t2 .. " wanted " .. t)
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

kvsdir_test (dir, key, "0",   "string 0 not converted to numeric")
kvsdir_test (dir, key, "0x1", "string 0x1 not converted to numeric")
kvsdir_test (dir, key, "00",  "string 00 not converted to numeric")


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

local kw,err = f:kvswatcher {
    key = data.key,
    handler = function (kw, result)
        type_ok (kw, 'userdata', "kvs watch userdata passed to handler")
        count = count + 1
        if count == 1 then
            is (result, nil, "kvswatcher: initial callback has result == nil")
        else
            is (result, data.value, "kvswatcher: 2nd callback has correct result")
            return f:reactor_stop()
        end
    end
}

type_ok (kw, 'userdata', "f:kvswatcher returns kvswatcher object")
kw.testkey = "foo"
is (kw.testkey, 'foo', "Can set arbitrary members of kvswatcher object")

os.execute (string.format ("flux kvs put %s=%s", data.key, data.value))
local r = f:reactor()

is (r, 0, "reactor exited normally")

ok (kw:remove(), "Can remove kvswatcher without error")


--
-- Again, but this time ensure callback is not called more than the
--  expected number of times.
--
local data = { key = "testkey2", value = "testvalue2" }

ncount = 0
dir [data.key] = nil -- Unlink current key
dir:commit()

local kw = f:kvswatcher {
    key = data.key,
    handler = function (kw, result)
        type_ok (kw, 'userdata', "kvs watch: userdata passed to handler", data.key)
        ncount = ncount + 1
        if ncount == 1 then
            is (result, nil, "kvswatcher: initial callback has result nil")
        elseif ncount == 2 then
            is (result, data.value, "kvswatcher: 2nd callback result="..result)
        end
    end
}

local t, err = f:timer {
    timeout = 1000,
    handler = function (f, to) return f:reactor_stop () end
}

-- Excute on rank 3 via flux-exec:
os.execute (string.format ("sleep 0.25 && flux exec -r 3 flux kvs put %s=%s",
    data.key, data.value))
local r = f:reactor()
is (r, 0, "reactor exited normally")
is (ncount, 2, "kvswatch callback invoked exactly twice")

note ("Ensure kvs watch callback not invoked after kvswatcher removal")
ok (kw:remove(), "Can remove kvswatcher without error")
os.execute (string.format ("sleep 0.25 && flux exec -r 3 flux kvs put %s=%s",
    data.key, 'test3'))
local r = f:reactor()
is (r, 0, "reactor exited normally")
is (ncount, 2, "kvswatch callback not invoked after kvs_unwatch")
is (dir [data.key], "test3", "but key value has been updated")


done_testing ()

-- vi: ts=4 sw=4 expandtab
