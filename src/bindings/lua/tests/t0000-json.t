#!/usr/bin/lua

require 'Test.More'
j = require 'jsontest'

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

plan 'no_plan'

-- Test equals
ok (equals({},{}), "equals works on empty tables" )
nok (equals({}, {"foo"}), "equals detects unequal tables")

-- Tests
for _,t in pairs (tests) do
  local r,err = j.runtest (t.value)
  type_ok (r, 'table', t.name .. ": result is a table")
  ok (equals (t.value, r),  t.name .. ": result is expected")
end

is (j.runtest ("x"), "x",     "string returns unharmed")
is (j.runtest (true), true,   "`true' value returns unharmed")
is (j.runtest (false),false,  "`false' value returns unharmed")
is (j.runtest (1), 1,         "number 1 returns unharmed")
is (j.runtest (0), 0,         "number 0 returns unharmed")
is (j.runtest (1.0), 1.0,     "float value returns unharmed")

todo ( "Need to investigate", 1)
is (j.runtest (1.01), 1.01,   "float value returns unharmed (more precision)")

is (j.runtest (1024000000), 1024000000,
			      "large value returns unharmed")
is (nil, j.runtest (nil),     "nil works")

done_testing ()
