#!/usr/bin/lua

require 'lunit'

local RDL = require 'RDL'

module ("TestRDL", lunit.testcase, package.seeall)

local function equals(t1, t2)
   if t1 == t2 then
       return true
   end
   if type(t1) ~= "table" or type(t2) ~= "table" then
       return false
   end
   local v2
   for k,v1 in pairs(t1) do
       v2 = t2[k]
       if v1 ~= v2 and not equals(v1, t2[k]) then
           return false
       end
   end
   for k in pairs(t2) do
       if t1[k] == nil then
           return false
       end
   end
   return true
end

function test_rdl()
    local rdl = RDL.eval([[
Hierarchy "default" { Resource {"node", name="foo", id=0, tags = { "bar" } }}
]])
    assert_not_nil (rdl)
    assert_true (is_table (rdl))
    local r = rdl:resource("default")

    assert_not_nil (r)
    assert_equal ("foo0", r.name)
    assert_equal ("node", r.type)
    assert_equal (0,      r.id)
    assert_equal ("default:/foo0", r.uri)
    assert_equal ("/foo0", r.path)
    assert_equal ("default", r.hierarchy_name)
    assert(is_table(r.tags), "r.tags is "..type(r.tags))
    assert (equals ({node = 1, bar = 1}, r.tags))
    assert (equals ({node = 1, bar = 1}, r:aggregate()))

    local t = r:tabulate()
    --print (require 'inspect' (t))
    assert_equal (r.type, t.type)
    assert_equal (r.id,   t.id)
    assert_equal (r.uuid, t.uuid)
    assert_equal (r.path, t.hierarchy [r.hierarchy_name])

    local uuid = r.uuid
    assert_equal (36, uuid:len())
    assert_equal (nil, uuid:find ("'[^a-f0-9-]"))

    r:delete_tag ("bar")
    assert (equals ({node = 1}, r.tags))

    r:tag ("baz", 100)
    local i = require 'inspect'
    assert (equals ({node = 1, baz = 100}, r.tags), "got:".. i(r.tags))

    r:delete_tag ("baz")
    assert (equals ({node = 1}, r.tags))
end

function test_has_socket()
    local rdl = RDL.eval([[uses "Socket"]])
    assert_not_nil (rdl)
end

function test_children()
    local rdl = RDL.eval([[
uses "Socket"
Hierarchy "default" {
  Resource {"node", name="foo", id=0,
   children = {
      Socket { id=0, cpus="0-3" },
      Socket { id=1, cpus="4-7" }
   }
}}
]])
    assert_not_nil (rdl)
    assert_true (is_table (rdl))
    local a = {
        node = 1,
        socket = 2,
        core = 8
    }
    local b = rdl:aggregate ('default')
    local i = require 'inspect'
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local r = rdl:resource ('default:/foo0')
    assert_not_nil (r)
    assert_equal ("foo0", r.name)
    assert_equal ("foo0", tostring (r))
    assert_equal ("node", r.type)
    assert_equal (0,      r.id)
    assert_equal ("default:/foo0", r.uri)
    assert_equal ("/foo0", r.path)
    assert_equal ("default", r.hierarchy_name)

    local b = r:aggregate()
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local s = r:next_child()
    assert_not_nil (s)
    assert_equal ("socket0", s.name)
    assert_equal ("socket0", tostring (s))
    assert_equal ("socket",  s.type)
    assert_equal (0,         s.id)
    assert_equal ("default:/foo0/socket0", s.uri)
    assert_equal ("/foo0/socket0", s.path)
    assert_equal ("default", s.hierarchy_name)

    local a = { socket = 1, core = 4 }
    local b = s:aggregate()
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local c = s:next_child()
    assert_not_nil (c)
    assert_equal ("core0", c.name)
    assert_equal ("core0", tostring (c))
    assert_equal ("core",  c.type)
    assert_equal (0,       c.id)
    assert_equal ("default:/foo0/socket0/core0", c.uri)
    assert_equal ("/foo0/socket0/core0", c.path)
    assert_equal ("default", c.hierarchy_name)

    local a = { core = 1 }
    local b = c:aggregate()
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    -- Test unlink of child from socket0
    --
    assert (s:unlink ("core1"))

    local a = { socket = 1, core = 3 }
    local b = s:aggregate()
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    -- Ensure next child is now core2 (we deleted core1)
    local c = s:next_child()
    assert_not_nil (c)
    assert_equal ("core2", c.name)
    assert_equal ("core2", tostring (c))
    assert_equal ("core",  c.type)
    assert_equal (2,       c.id)
    assert_equal ("default:/foo0/socket0/core2", c.uri)
    assert_equal ("/foo0/socket0/core2", c.path)
    assert_equal ("default", c.hierarchy_name)

    -- Top level aggregate should now be updated
    local a = { node = 1, socket = 2, core = 7 }
    local b = assert (rdl:aggregate ('default'))
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

  end

function test_copy ()
    local rdl = RDL.eval([[
uses "Socket"
Hierarchy "default" {
  Resource {"node", name="foo", id=0,
   children = {
      Socket { id=0, cpus="0-3" },
      Socket { id=1, cpus="4-7" }
   }
}}
]])
    assert_not_nil (rdl)
    assert_true (is_table (rdl))
    local a = {
        node = 1,
        socket = 2,
        core = 8
    }
    local b = rdl:aggregate ('default')
    local i = require 'inspect'
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))


    -- Copy:
    local slice = rdl:copy ("default:/foo0/socket1")

    assert_not_nil (slice)
    assert_true (is_table (slice))
    local a = {
        node = 1,
        socket = 1,
        core = 4
    }
    local b = slice:aggregate ('default')
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local r = assert (slice:resource ("default:/foo0/socket1"))
    assert_equal ("socket1", r.name)

    -- Test dup()
    local slice = rdl:dup()
    assert_not_nil (slice)
    assert_true (is_table (slice))
    local a = {
        node = 1,
        socket = 2,
        core = 8
    }
    local b = slice:aggregate ('default')
    local i = require 'inspect'
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))


    -- test equals
    assert_equal (slice, rdl, "Failed equality")
    assert (rdl:compare (slice), "failed compare")

    local new = assert (slice:copy ("default:/foo0/socket1"))
    assert_false (slice == new, "Objects should not be equal but were")
end

function test_ListOf()
    local rdl = assert (RDL.eval ([[
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Resource, ids="0-99", args = {"bar"} } }
    }
}
]]))
    assert_not_nil (rdl)
    assert_true (is_table (rdl))
    local a = {
        foo = 1,
        bar = 100,
    }
    local b = rdl:aggregate ('default')
    local i = require 'inspect'
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local r = assert (rdl:resource ("default:/foo/bar99"))
    assert_not_nil (r)
    assert_equal ("bar99", r.name)
    assert_equal ("bar99", tostring (r))
    assert_equal ("bar",   r.type)
    assert_equal (99,      r.id)
    assert_equal ("default:/foo/bar99", r.uri)
    assert_equal ("/foo/bar99", r.path)
    assert_equal ("default", r.hierarchy_name)

end


function test_find()
    local rdl = assert (RDL.eval ([[
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Resource, ids="0-99", args = {"bar"} } }
    }
}
]]))
    assert_not_nil (rdl)
    assert_true (is_table (rdl))

    -- Find by ID:
    local rdl2 = assert (rdl:find{ type = bar, ids = "1,5,9" })

    local a = {
        foo = 1,
        bar = 3
    }
    b = rdl2:aggregate('default')
    local i = require 'inspect'
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    for _,id in pairs({1,5,9}) do
        local name = "bar"..id
        local r = assert (rdl2:resource ("default:/foo/"..name))
        assert_not_nil (r)
        assert_equal (name, r.name)
        assert_equal (name, tostring (r))
        assert_equal ("bar",   r.type)
        assert_equal (id,      r.id)
        assert_equal ("default:/foo/"..name, r.uri)
        assert_equal ("/foo/"..name, r.path)
        assert_equal ("default", r.hierarchy_name)
    end

    -- Find by name
    rdl2 = assert (rdl:find{ name="bar[5-10]" })

    local a = {
        foo = 1,
        bar = 6
    }
    b = assert (rdl2:aggregate('default'))
    assert (equals (a, b), "Expected ".. i(a) .. " got ".. i(b))

    local hl = require 'hostlist'.new ("bar[5-10]")

    for name in hl:next() do
        local id = tonumber (name:match ("[0-9]+$"))
        local r = assert (rdl2:resource ("default:/foo/"..name))
        assert_not_nil (r)
        assert_equal (name, r.name)
        assert_equal (name, tostring (r))
        assert_equal ("bar",   r.type)
        assert_equal ("bar",   r.basename)
        assert_equal (id,      r.id)
        assert_equal ("default:/foo/"..name, r.uri)
        assert_equal ("/foo/"..name, r.path)
        assert_equal ("default", r.hierarchy_name)

    end

end

function test_find_time ()
    local rdl = assert (RDL.eval ([[
uses "Node"
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Node, ids="0-10",
                             args = { name="bar",
                                      sockets = { "0-7", "8-16"}
                                    }
                           }
                   }
    }
}
]]))
    assert_not_nil (rdl)
    assert_true (is_table (rdl))

    local t0 = require 'timer'.new()
    local r = assert (rdl:find{ type = "core" })
    local delta = t0:get0()
    assert_true (delta < 0.02, "rdl_find took "..delta.." seconds")
end

function test_accumulator()
    local rdl = assert (RDL.eval ([[
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Resource, ids="0-99", args = {"bar"} } }
    }
}
]]))
    assert_not_nil (rdl)
    assert_true (is_table (rdl))

    local a = rdl:resource_accumulator ()
    assert_not_nil (a)

    local r = rdl:resource ("default:/foo/bar10")
    assert_not_nil (r)

    -- check by resource:
    assert (a:add (r))
    assert (a:add (r.uuid))

    local rdl2 = assert(a:dup ())
    assert_not_nil (rdl2)

    local y = rdl2:aggregate ("default")
    local c = {
        foo = 1,
        bar = 1,
    }

    local b = rdl2:aggregate ('default')
    local i = require 'inspect'
    assert (equals (c, b), "Expected ".. i(c) .. " got ".. i(b))
    assert (rdl2:find ("default:/foo/bar10"))

    local s = a:serialize()
    local rdl3 = RDL.eval (s)

    assert_not_nil (s)
    assert_not_nil (rdl3)
    local b = rdl2:aggregate ('default')
    assert (equals (c, b), "Expected ".. i(a) .. " got ".. i(b))
    assert (rdl2:find ("default:/foo/bar10"))

end

local function print_resource (r, pad)
    local p = pad or ""
    local s = string.format ("%s%s\n", (pad or ""), r.name)
    io.stderr:write (s)
    for c in r:children() do
        print_resource (c, p.." ")
    end
end

local function rdl_print (rdl)
    local r = rdl:resource ("default")
    io.stderr:write ("\n")
    print_resource (r)
end

local function assert_aggregate (r, expected)
    local result = r:aggregate ('default')
    assert_not_nil (result)
    local i = require 'inspect'
    assert (equals (expected, result),
        "Expected "..i(expected).. " got"..i(result))
end

function test_accumulator2()
  local rdl = assert (RDL.eval ([[
uses "Node"
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Node, ids="0-10",
                             args = { name="bar",
                                      sockets = { "0-7", "8-16"}
                                    }
                           }
                   }
    }
}
]]))
    assert_not_nil (rdl)
    assert_true (is_table (rdl))

    local a = rdl:resource_accumulator()
    assert_not_nil (a)

    local r = rdl:resource ("default:/foo/bar1/socket0/core0")
    assert (a:add (r))
    assert_aggregate (a, {socket=1,core=1,node=1,foo=1})

    r = rdl:resource ("default:/foo/bar1/socket0/core1")
    assert_not_nil (r)
    assert (r.name == "core1")
    assert (a:add (r))
    assert (a:resource ("default:/foo/bar1/socket0/core1"))
    assert_aggregate (a, {socket=1,core=2,node=1,foo=1})
end

function test_aggregate()
    local rdl = assert (RDL.eval ([[
Hierarchy "default" {
    Resource{
        "foo",
        children = { ListOf{ Resource, ids="0-99",
                     args = {"bar", tags = { count = 5 } } } }
    }
}
]]))
    local expected = { bar = 100, foo = 1, count = 500 }

    assert_true (is_table (rdl))
    local a = rdl:aggregate ("default")
    local i  = require 'inspect'
    assert (equals (a, expected), 'Expected '..i(expected) .. ' Got ' .. i(a))
end

-- vi: ts=4 sw=4 expandtab
