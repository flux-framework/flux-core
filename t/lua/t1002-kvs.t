#!/usr/bin/env lua

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

local r, err = dir:unlink (key)
is (r, true, "kvsdir: unlink succeeds")
is (err, nil, "kvsdir: unlink: no error")
dir:commit()
local r = dir [key]
is (r, nil, "unlink: after unlink key doesn't exist")

-- low-level kvs_put testing
local tests = {
 { val = "foo", msg = "kvs_put: string" },
 { val = 42,    msg = "kvs_put: numeric"},
 { val = true,  msg = "kvs_put: true" },
 { val = false, msg = "kvs_put: false" },
 { val = { x = 1, y = 2 }, msg = "kvs_put: a table" },
 { val = { 1, 2, 3, 4, 5 }, msg = "kvs_put: an array" },
}

for _, t in pairs(tests) do
    ok (f:kvs_put (key, t.val),  t.msg)
    f:kvs_commit()
    local v = dir[key]
    is (type (v), type (t.val), t.msg ..": type match")
    if type (t.val) == 'table' then
        is_deeply (v, t.val, t.msg .. " table values match")
    else
        is (v, t.val, t.msg .. " values match")
    end
end

-- low level kvs_get testing
for _, t in pairs (tests) do
    local msg = "kvs_get:"..t.msg:match ("kvs_put:(.+)$")
    ok (f:kvs_put (key, t.val),  t.msg)
    f:kvs_commit()
    local v, err = f:kvs_get (key)
    ok (v ~= nil, msg)
    is (type (v), type (t.val), msg ..": type match")
    if type (t.val) == 'table' then
        is_deeply (v, t.val, msg .. " table values match")
    else
        is (v, t.val, msg .. " values match")
    end
end

-- symlink testing
dir.target = "this is the target"
ok (f:kvs_symlink ("link", "target"), "kvs_symlink ()")
ok (f:kvs_commit (), "kvs commit")
is (dir.link, dir.target, "symlink returns contents of target")
local t, x = f:kvs_type ("link")
is (t, "symlink", "type of link is "..tostring (t))
is (x, "target", "contents of symlink is target")

-- type testing
dir ["t.foo"] = "bar"
dir:commit()
local t, x = f:kvs_type ("t")
is (t, "dir", "type of t is now "..tostring (t))

dir.t = "bar"
dir:commit ()
local t, x = f:kvs_type ("t")
is (t, "file", "type of t is now "..tostring (t))

local t, x = f:kvs_type ("missing")
is (t, nil, "kvs_type on missing key returns error")
is (type(x), "string", "error is a string")

-- unlink testing
dir.testkey = "foo"
kvsdir_test (dir, 'testkey', "foo", "unlink: set known value for key")
dir.testkey = nil
kvsdir_test (dir, 'testkey', nil, "unlink: key is now nil")

dir.testkey = "foo"
kvsdir_test (dir, 'testkey', "foo", "unlink: set known value for key")
local r, err = f:kvs_unlink ("testkey")
is (r, true, "kvs_unlink: success")
is (err, nil, "kvs_unlink: no error")
f:kvs_commit()
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

-- kvswatch on directory should fail:
f:kvs_put ("testdir.value", 42)
f:kvs_commit ()
local kw, err = f:kvswatcher {
    key = "testdir",
    handler = function () end
}
is (kw, nil, 'Error expected with kvswatch on directory')
is (err, "Is a directory", 'Error message matches')

local kw, err = f:kvswatcher {
    key = "testdir",
    isdir = true,
    handler = function (kw, result)
        type_ok (result, 'userdata', "result is kvsdir")
        is (result.value, 42, "directory contents are as expected")
    end
}
type_ok (kw, 'userdata', "f:kvswatcher with isdir kvswatcher object")
is (err, nil, "no error from kvswatcher with isdir")

-- Force creation of new handle and reactor here so that
--  reactor time is guaranteed to be updated, and our timeout
--  used below is relative to now and not last active reactor time.
--
local f = check_userdata_return ("flux.new", flux.new())
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
            f:reactor_stop ()
        end
    end
}

type_ok (kw, 'userdata', "f:kvswatcher returns kvswatcher object")
kw.testkey = "foo"
is (kw.testkey, 'foo', "Can set arbitrary members of kvswatcher object")

os.execute (string.format ("flux kvs put --json %s=%s", data.key, data.value))

local to = f:timer {
    timeout = 1500,
    oneshot = true,
    handler = function (f, to)
        f:reactor_stop_error ("Timed out after 1.5s!")
    end
}
local r, err = f:reactor()

isnt (r, -1, "reactor exited normally with "..(r and r or err))
to:remove()

ok (kw:remove(), "Can remove kvswatcher without error")

-- Again, force creation of new handle and reactor here so that
--  reactor time is guarateed to be updated, and our timeout
--  used below is relative to now and not last active reactor time.
--
local f = check_userdata_return ("flux.new", flux.new())

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
os.execute (string.format ("sleep 0.25 && flux exec -r 3 flux kvs put --json %s=%s",
    data.key, data.value))
local r, err = f:reactor()
isnt (r, -1, "reactor exited normally with ".. (r and r or err))
is (ncount, 2, "kvswatch callback invoked exactly twice")

note ("Ensure kvs watch callback not invoked after kvswatcher removal")
ok (kw:remove(), "Can remove kvswatcher without error")
os.execute (string.format ("sleep 0.25 && flux exec -r 3 flux kvs put --json %s=%s",
    data.key, 'test3'))

local t, err = f:timer {
    timeout = 500,
    handler = function (f, to) return f:reactor_stop () end
}
local r, err = f:reactor()
isnt (r, -1, "reactor exited normally with "..(r and r or err))
is (ncount, 2, "kvswatch callback not invoked after kvs_unwatch")
is (dir [data.key], "test3", "but key value has been updated")


done_testing ()

-- vi: ts=4 sw=4 expandtab
