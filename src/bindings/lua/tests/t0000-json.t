#!/usr/bin/lua

require 'lunit'

local j = require 'jsontest'

module ("TestJsonLua", lunit.testcase, package.seeall)

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

--~ print a table
function printTable(list, i)

    local listString = ''
--~ begin of the list so write the {
    if not i then
        listString = listString .. '{'
    end

    i = i or 1
    local element = list[i]

--~ it may be the end of the list
    if not element then
        return listString .. '}'
    end
--~ if the element is a list too call it recursively
    if(type(element) == 'table') then
        listString = listString .. printTable(element)
    else
        listString = listString .. element
    end

    return listString .. ', ' .. printTable(list, i + 1)

end

local tests = {
	{ name  = "empty array", value = {} },

	{ name  = "array",       value = { "one", "two", "three" } },

	{ name  = "table",
	  value = { one = "one", two = "two", three = "three" }},

	{ name  = "nested table",
	  value = { table1 = { a = "x", b = "y", c = "z" },
                    array = { one, two, three } }},

	{ name  = "table with empty tables",
	  value = { one = {}, two = "two", three = "three" }},
}


function test_equals()
	assert_true (equals({},{}), "equals works on empty tables" )
	assert_false (equals({}, {"foo"}, "equals detects unequal tables"))
end

function test_tables()
	for _,t in pairs (tests) do
          local r,err = j.runtest (t.value)
	  assert_true (is_table (r))
	  assert_true (equals (t.value, r))
	end
end

function test_values()
	assert_equal ("x", j.runtest ("x"))
        assert_equal (true, j.runtest (true))
        assert_equal (false, j.runtest (false))
        assert_equal (1, j.runtest (1))
        assert_equal (0, j.runtest (0))
        assert_equal (1.0, j.runtest (1.0))
        assert_equal (1024000000, j.runtest (1024000000))
        assert_equal (nil, j.runtest (nil))
end
