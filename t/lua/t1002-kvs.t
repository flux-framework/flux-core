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

done_testing()

-- vi: ts=4 sw=4 expandtab
