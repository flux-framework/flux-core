#!/usr/bin/env lua
require "Test.More"

local affinity = require 'affinity'
ok (affinity, "require 'affinity'")
local cpu_set = affinity.cpuset
ok (cpu_set, "set affinity.cpuset to cpu_set")

local fmt = string.format
plan ('no_plan')

TestCpuSet = {

    to_string = {
        { cpus={0,1,2,3}, result="0-3",    count=4 },
        { cpus={0,2,4},   result="0,2,4",  count=3 },
        { cpus={},        result="",       count=0 },
        { cpus={0,1},     result="0,1",    count=2 },
    },

    new = {

        -- String input:
        { input="0-3",               output="0-3" },
        { input="0-20:4",            output="0,4,8,12,16,20" },
        { input="11",                output="11"  },
        { input="255",               output="255"  },
        { input="0xf",               output="0-3" },
        { input="f",                 output="0-3" },
        { input="0f",                output="0-3" },
        { input="0xff",              output="0-7" },
        { input="ff",                output="0-7" },
        { input="0,1,2,3",           output="0-3" },
        { input="0000000f",          output="0-3" },
        { input="0x0000000f",        output="0-3" },
        { input="0xff00000000000000",output="56-63" },

        -- Bitstring input:
        { input="00000000,00000000,00000000,00000000,00000000,00000000,"
              .."00000000,0000ffff", output="0-15" },
        { input="000000ff,00000000,00000000,00000000,00000000,00000000,"
              .."00000000,00000000", output="224-231" },
        { input="10000000,10000000,10000000,10000000,10000000,10000000,"
              .."10000000,10000000",
              output="28,60,92,124,156,188,220,252" },
        { input="10000000,01000000,00100000,00010000,00001000,00000100,"
              .."00000010,00000001",
              output="0,36,72,108,144,180,216,252" },

        -- Input by numbers (raw bitmask)
        { input=0xff,                output="0-7"  },
        { input=255,                 output="0-7"  },
        { input=0x0,                 output=""     },
    },

    eq = {
        { s1='0-3',   s2='0-3',   r=true },
        { s1="0xff",  s2="0-7",   r=true },
        { s1="0x1",   s2="0x1",   r=true },
        { s1="0,100", s2="0,100", r=true },
        { s1="",      s2="",      r=true },
        { s1="",      s2="0x0",   r=true },
        { s1="",      s2="0x1",   r=false },
        { s1="1-3",   s2="0",     r=false },
    },

    len = {
        { arg="0xffff", count=16 },
        { arg="0,1,2",  count=3  },
        { arg="0,1000", count=2  },
        { arg=nil,      count=0  },
    },

    add = {
        { a="0-3", b="",     result="0-3" },
        { a="0-3", b="0-3",   result="0-3" },
        { a="0-3", b="1-4",   result="0-4" },
        { a="0-3", b="4-7",   result="0-7" },
        { a="0-3", b="7",     result="0-3,7" },
    },

    subtract = {
        { a="0-3", b="",      result="0-3" },
        { a="0-3", b="0-3",   result=""    },
        { a="0-3", b="1-4",   result="0"   },
        { a="0-3", b="4-7",   result="0-3" },
        { a="0-3", b="1,2",   result="0,3" },
    },
}

local function fails_with (fn, params, pattern, msg)
    local r, err = fn (unpack(params))
    if r then
        fail (msg)
        diag ("    unexpected success\n" ..
              "    expected: ".. tostring (pattern) .. " got " .. tostring (r))
    else
        local pass = false
        if err then pass = err:match (pattern) end
        ok (pass, msg)
        if not pass then
            diag ("    '"..tostring (err).."' doesn't match '"..pattern.."'")
        end
    end
end


function test_cpu_set_constants ()
    type_ok (cpu_set.SETSIZE, 'number', "SETSIZE is a number")
    ok (cpu_set.SETSIZE > 0 and cpu_set.SETSIZE < 2000, "SETSIZE seems valid")
    local c = cpu_set.new()
    ok (c, "create a new cpu_set")
    type_ok (c.size, 'number')
    ok (c.size > 0 and c.size < 2000, "cset.size seems valid")
end

function test_cpu_set_error()
    local new = cpu_set.new
    local x, y = new ("foo");
    is (x, nil, "Invalid string passed to cset.new()")
    type_ok (y, "string", "Got an error message")
    like (y, "unable to parse CPU mask or list:", "got error: "..y)
    local x, y = cpu_set.union ("0x1", "f-10")
    is (x, nil, "Invalid args to cpus_set.union")
    type_ok (y, "string", "Got an error message")
    like (y, "unable to parse", "got error: "..y)
    local x, y = cpu_set.intersect ("0x1", "f-10")
    is (x, nil, "Invalid args passed to intersect")
    type_ok (y, 'string', "Got an error message")
    like (y, "unable to parse", "got error: "..y)

    -- cpuset too big
    fails_with (new, { '1-1024' }, "unable to parse",
                "large cpuset gives an error")

    fails_with (new, { "1025" }, "unable to parse",
                "large cpuset single number gives an error")

    -- Using : by accident
    error_like (new, { cpu_set, '0-3' },
            "Table is 1st arg to new()",
            "Table is 1st argument to new()")
    -- Wrong number of args
    error_like (cpu_set.new, { '0-3', '4-7' },
            "Expected < 2 arguments to new",
            "> 1 arg should be an error")
    --
    fails_with (cpu_set.union, { "foo", "0-3" },
            "unable to parse",
            "Invalid arg to union is erorr")
    --
    fails_with (cpu_set.intersect, { "foo", "0-3" },
            "unable to parse",
            "Invalid arg to intersect is error")
    --
    fails_with (cpu_set.new, { 0xffffffffffffffff },
            "numeric overflow",
            "large value to new causes numeric overflow")
    -- Invalid index
    error_like (function () c = cpu_set.new(); s=c[2048] end, {},
            "Invalid index 2048 to cpu_set",
            "Invalid index in assignment")

    error_like (function () c = cpu_set.new(); c[2048]=1 end, {},
            "Invalid index 2048 to cpu_set",
            "Invalid index")

    error_like (function () c = cpu_set.new(); c[0]=3 end, {},
            "Index of cpu_set may only be set to 0",
            "Invalid index in newindex")
end

function test_new_nil_args()
    type_ok (cpu_set.new(), 'userdata', "new() returns userdata")
    type_ok (cpu_set.new(nil), 'userdata', "new(nil) returns userdata")
    is (cpu_set.new():count(), 0, "size of new cpu_set is 0")

    local c = cpu_set.new()
    type_ok (c, 'userdata', 'new() returns userdata')
    is (#c, 0, "length operator works")
    is (tostring(c), "", "tostring returns empty string")

    local c = cpu_set.new(nil)
    type_ok (c, 'userdata', 'new(nil) returns userdata')
    is (#c, 0, "length operator works")
    is (tostring(c), "", "tostring returns empty string")

    local c = cpu_set.new("")
    type_ok (c, 'userdata', 'new(nil) returns userdata')
    is (#c, 0, "length operator works")
    is (tostring(c), "", "tostring returns empty string")
end

function test_to_string()
    for _,s in pairs (TestCpuSet.to_string) do
        local c = cpu_set.new()
        type_ok (c, 'userdata', fmt ("checking %s..", s.result))
        for _,cpu in pairs (s.cpus) do c:set(cpu) end
        is (tostring (c), s.result, "tostring (s) = " .. s.result)
        is (#c, s.count, "result is %d bits", s.count)
    end
end

function test_cpu_set_new()
    for _,t in pairs (TestCpuSet.new) do
        local c,err = cpu_set.new(t.input)
        type_ok (c, 'userdata', fmt ("new (%s) returns userdata", t.input))
        is (tostring(c), t.output, fmt("cpu_set.new('%s') is %s,",
					t.input, t.output))
    end
end

function test_cpu_set_copy()
    for _,t in pairs (TestCpuSet.new) do
        local c,err = cpu_set.new(t.input)
        type_ok (c, 'userdata', fmt ("new(%s) is userdata", t.input))
        local copy = c:copy()
        type_ok (copy, 'userdata', 'copy is userdata')
        is (tostring(copy), t.output,
                fmt("cpu_set.new('%s') == %s", t.input, t.output))
    end
end

function test_cpu_set_eq()
    for _,t in pairs (TestCpuSet.eq) do
        local a = cpu_set.new(t.s1)
        local b = cpu_set.new(t.s2)
        type_ok (a, 'userdata', fmt ("new(%s) works", t.s1))
        type_ok (b, 'userdata', fmt ("new(%s) works", t.s1))
        if (t.r == true) then
            ok (a == b, "cpuset "..t.s1.." == "..t.s2)
        else
            nok (a == b, "cpuset "..t.s1.." != "..t.s2)
        end
    end
end

function test_cpu_set_len()
    for _,t in pairs (TestCpuSet.len) do
        local c = cpu_set.new(t.arg)
        type_ok (c, 'userdata')

        local msg = string.format("c=%s: count(c) == %d (got %d)",
                tostring(c), t.count, #c)
        ok (t.count == #c, msg)

        local msg = string.format("c=%s: count(c) == %d (got %d)",
                tostring(c), t.count, c:count())
        ok (t.count == c:count(), msg)
    end
end

function test_cpu_set_add()
    for _,t in pairs (TestCpuSet.add) do
        local a = cpu_set.new(t.a)
        local b = cpu_set.new(t.b)
        type_ok (a, 'userdata')
        type_ok (b, 'userdata')
        local msg = string.format ("a(%s) + b(%s) == %s (got %s)",
                tostring(a), tostring(b), t.result, tostring(c))
        local c = a + b
        type_ok (c, 'userdata')
        ok (tostring(c) == t.result, msg)
    end
end

function test_cpu_set_subtract()
    for _,t in pairs (TestCpuSet.subtract) do
        local a, err = cpu_set.new(t.a)
        local b, err = cpu_set.new(t.b)
        type_ok (a, 'userdata', fmt("cpu_set.new('%s')", t.a))
        type_ok (b, 'userdata', fmt("cpu_set.new('%s')", t.b))
        local msg = string.format ("a(%s) - b(%s) == %s (got %s)",
                tostring(a), tostring(b), t.result, tostring(c))
        local c = a - b
        type_ok (c, 'userdata')
        ok (tostring(c) == t.result, msg)
    end

end

TestCpuSet.index = {
    { set = nil,   args = { [0] = false, [1] = false, [10] = false } },
    { set = "0-4", args = { [0] = true,  [1] = true,  [10] = false } },
}

function test_cpu_set_index()
    for _,t in pairs (TestCpuSet.index) do
        local c = cpu_set.new(t.set)
        type_ok (c, 'userdata', fmt ("new (%s)", tostring (t.set)))
        for i,r in pairs (t.args) do
            is (c[i], r,
                    fmt ("set %s: c[%d] is %s (expected %s)",
                        tostring (c), i, tostring(c[i]), tostring (r)))
        end
    end
end

TestCpuSet.newindex = {
    { set = nil,   args = { [0] = 1, [1] = 1, [3] = 1 }, result="0,1,3" },
    { set = "0-3", args = { [2] = 0, }, result="0,1,3" },
    { set = "0-3", args = { [2] = false, }, result="0,1,3" },
}

function test_cpu_set_newindex()
    for _,t in pairs (TestCpuSet.newindex) do
        local c = cpu_set.new(t.set)
        type_ok (c, 'userdata', fmt ("new (%s)", tostring (t.set)))
        for i,v in pairs (t.args) do
            c[i] = v
        end
        is (tostring (c), t.result,
                "got set " .. c .. "expected " .. t.result)
    end
end

TestCpuSet.set = {
    { set="",  args={0,1,2,3,4,5,6,7}, result="0-7" },
    { set="",  args={0,7},            result="0,7" },
}

function test_cpu_set_set()
    for _,t in pairs (TestCpuSet.set) do
        local c = cpu_set.new(t.set)
        type_ok (c, 'userdata', fmt ("new (%s)", tostring (t.set)))
        c:set(unpack(t.args))
        is (tostring(c), t.result,
                "got set " .. c .. "expected " .. t.result)
    end
end

TestCpuSet.clr = {
        { set="0-3", args={0}, result="1-3" },
        { set="0-3", args={0,3}, result="1,2" },
        { set="0-3", args={0,1,2,3}, result="" },
}

function test_cpu_set_clr ()
    for _,t in pairs (TestCpuSet.clr) do
        local c = cpu_set.new(t.set)
        type_ok (c, 'userdata', fmt ("new (%s)", t.set))
        c:clr (unpack(t.args))
        is (tostring(c), t.result,
                "got set " .. c .. "expected " .. t.result)
    end
end

TestCpuSet.isset = {
    { set="0-3", args={[0]=1,1,1,1,0,0,0}  },
    { set="0,2,4", args={[0]=1,0,1,0,1} },
}

function test_cpu_set_isset()
    for _,t in pairs (TestCpuSet.index) do
        local c = cpu_set.new(t.set)
        type_ok (c, 'userdata', fmt ("new (%s)", tostring (t.set)))
        for i,r in pairs (t.args) do
            is (c:isset(i), r,
                fmt ("set %s: c:isset(%d) is %s (expected %s)",
		             tostring (c), i, tostring(c:isset(i)), tostring (r)))
        end
    end
end

function test_cpu_set_zero()
    local c = cpu_set.new("0-128")
    type_ok (c, 'userdata', fmt ("new (%s)", '0-128'))
    is (tostring (c), "0-128", "tostring (c) works")
    is (#c, 129, "weight of c is 129")
    c:zero()
    is (#c, 0, "weight of c is now 0")
    is (tostring(c), "", "tostring (c) returns empty string")
end

TestCpuSet.union = {
    { a="0-3", args={"0-10"}, result="0-10" },
    { a="",    args={"1-3"},  result="1-3" },
    { a="0-3", args={""},     result="0-3" },
    { a="0-3", args={"1-4"},  result="0-4" },
    { a="0-3", args={"1","7","8"},  result="0-3,7,8" },
}

function test_cpu_set_union()
    for _,t in pairs (TestCpuSet.union) do
        local r = { cpu_set.new(t.a), cpu_set.union (t.a, unpack(t.args)) }
        type_ok (r[1], 'userdata', fmt ('new (%s)', t.a))
        type_ok (r[2], 'userdata', fmt ('union (a, args..)'))
        r[1]:union(unpack(t.args))
        for _,c in pairs(r) do
            ok (tostring(c) == t.result,
                fmt ("\"%s\":union(%s) == %s (got %s)",
                    t.a, table.concat(t.args),t.result, tostring(c)))
        end
    end
end

TestCpuSet.intersect = {
    { a="0-3", args={"0-7"}, result="0-3" },
    { a="0-3", args={"2-5"}, result="2,3" },
    { a="0-7", args={""},    result=""    },
    { a="0-7", args={"11"},  result=""    },
}

function test_cpu_set_intersect()
    for _,t in pairs (TestCpuSet.intersect) do
        local x = cpu_set.new(t.a)
        local y = cpu_set.intersect(t.a, unpack(t.args))
        type_ok (x, 'userdata', fmt ("new (%s)", t.a))
        type_ok (y, 'userdata', fmt ("intersect (%s, %s)", t.a, t.args[1]))
        x:intersect(unpack(t.args))
        for _, c in pairs ({ x, y }) do
            ok (tostring(c) == t.result,
                string.format ("\"%s\":intersect(%s) == %s (got '%s')",
                    t.a, table.concat(t.args),t.result, tostring(c)))
        end
    end
end

TestCpuSet.is_in = {
    { a="0-3",  b="0xff",  result=true },
    { a="0-7",  b="0xff",  result=true },
    { a="0-15", b="0xff",  result=false},
    { a="0-3",  b="0-3",   result=true },  -- set contains itself
    { a="",     b="0xff",  result=true },  -- empty set is an any other set
    { a="1",    b="",      result=false }, -- nothing in empty set
}

function test_cpu_set_is_in()
    for _,t in pairs (TestCpuSet.is_in) do
        local c = cpu_set.new(t.a)
        type_ok (c, 'userdata', fmt ("new (%s)", t.a))
        is (c:is_in(t.b), t.result,
                fmt ("%s:is_in (%s) == %s (got %s)",
                     t.a, t.b, tostring(t.result), tostring(c:is_in(t.b))))
    end
end

TestCpuSet.contains = {
    { a="0-3",  b="0xff", result=false },
    { a="0xff", b="0xff", result=true  },
    { a="0-3",  b="0-3",  result=true  },
    { a="0-3",  b="4",    result=false },
    { a="",     b="1",    result=false },
    { a="0-3",  b="",     result=true  },
}

function test_cpu_set_contains()
    for _,t in pairs (TestCpuSet.contains) do
        local c = cpu_set.new(t.a)
        type_ok (c, 'userdata', fmt ("new (%s)", t.a))
        ok (c:contains(t.b) == t.result,
                string.format ("%s:contains (%s) == %s (got %s)",
                    t.a, t.b, tostring(t.result), tostring(c:is_in(t.b))))
    end
end

function test_cpu_set_iterator()
    for _,t in pairs (TestCpuSet.new) do
        local c = cpu_set.new(t.input)
        type_ok (c, 'userdata', fmt ("cpu_set.new('%s')", t.input))
        local count = 0
        for i in c:iterator() do
            type_ok (i, 'number', i.." is a number")
            count = count + 1
            ok (c[i],
                    fmt ("iterator returned cpu%d", i))
        end
        local n = #c
        is (n, count,
                fmt ("iterator gets %d values for '%s'", count, tostring (c)))
    end
end


-- Function to return only odd numbers
local odd = function (i) return i%2 ~= 0 and i end
local endzero = function (i) return tostring(i):match("0$") and i end


TestCpuSet.expand = {
    { input="0-7", fn=nil,      n=8, out="0,1,2,3,4,5,6,7" },
    { input="0xf", fn=nil,      n=4, out="0,1,2,3"         },
    { input="0xf", fn=odd,      n=2, out="1,3"             },
    { input="",    fn=nil,      n=0, out=""                },
    { input="500", fn=nil,      n=1, out="500"             },
    { input="1-100",
      fn=endzero,
      n=10,
      out="10,20,30,40,50,60,70,80,90,100"                 },
}

function test_cpu_set_expand()
    for _,t in pairs (TestCpuSet.expand) do
        local c = cpu_set.new(t.input)
        type_ok (c, 'userdata', fmt ("cpu_set.new('%s')", t.input))
        local tbl = c:expand (t.fn)
        type_ok (tbl, 'table', "expand returns table")
        is (#tbl, t.n,
            fmt ("%s: check table of size %d", t.input, t.n))
        is (t.out, table.concat (tbl, ","), fmt ("expand returns %s", t.out))
    end
end


TestCpuSet.firstlast = {
    { input="0-7", first=0,   last=7 },
    { input="11",  first=11,  last=11 },
    { input="",    first=nil, last=nil },
}

function test_cpu_set_firstlast()
    for _,t in pairs (TestCpuSet.firstlast) do
        local c = cpu_set.new (t.input)
        type_ok (c, 'userdata', fmt ("cpu_set.new('%s')", t.input))
        is (c:first(), t.first, "first is "..tostring (t.first))
        is (c:last(), t.last,   "last is " ..tostring (t.last))
    end
end

function test_cpu_set_tohex()
    for _,t in pairs (TestCpuSet.new) do
        local c = cpu_set.new (t.input)
        type_ok (c, 'userdata', fmt ("cpu_set.new('%s')", t.input))
        local hex = c:tohex()
        type_ok (hex, 'string', "c:tohex() returns string")
        local new, err = cpu_set.new (hex)
        type_ok (new, 'userdata', fmt ("new (%s)", hex))
        ok (new == c, fmt("(%s)[%s] == (%s)[%s]",
                                   t.input, tostring(c), hex, tostring(new)))
    end
end

function affinity_tests ()
    local orig, err = affinity.getaffinity ()
    type_ok (orig, 'userdata', "getaffinity: returns userdata")
    if not orig then
        diag ("getaffinity failed with "..err)
    end

    local new = orig:first()
    local c, err = affinity.setaffinity (tostring (new))
    is (c, true, "setaffinity ("..new..") works (c = "..tostring (c).. ")")
    if not c then
        diag ("setaffinity failed with "..err)
    end
    is (tostring (affinity.getaffinity ()), tostring (new),
        "getaffinity returns 0: works")

    fails_with (affinity.setaffinity, { '1025-1026' },
                "unable to parse",
                "setaffinity fails with invalid cpuset")

    fails_with (affinity.setaffinity, { '510,511' },
                "sched_setaffinity: Invalid argument",
                "setaffinity fails with invalid bits")

    fails_with (affinity.setaffinity, { "" },
                "sched_setaffinity: Invalid argument",
                "setaffinity fails with empty cpuset")

    ok (affinity.setaffinity (orig), 'restore original affinity')

end


for k,v in pairs (_G) do
    if k:match ("^test_") and type (v) == 'function' then
        subtest (k, v)
    end
end

subtest ("affinity tests", affinity_tests)


done_testing ()
-- vi: ts=4 sw=4 expandtab
