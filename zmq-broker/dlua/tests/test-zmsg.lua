#!/usr/bin/lua

require 'lunit'

package.cpath = "../?.so"
local z = require 'zmsgtest'

module ("TestZMSGLua", lunit.testcase, package.seeall)

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

function test_equals()
	assert_true (equals({},{}), "equals works on empty tables" )
	assert_false (equals({}, {"foo"}, "equals detects unequal tables"))
end


function test_zmsg()
	local data = { one = "foo", two = "bar" }
	local msg = z.req ("test.tag", data)
	assert_userdata (msg)
	assert_equal ("test.tag", msg.tag)
	assert_equal ("request",  msg.type)
	assert_true  (equals (data, msg.data))
end

local ftypes = {
	request = z.req,
	response = z.resp,
	event = z.event,
	snoop = z.snoop
}

function test_types()
	local data = {}
	for t,f in pairs (ftypes) do
		local msg,err = f ("test.tag", data)
		if not msg then fail (err) end
		assert_userdata (msg)
		assert_equal ("test.tag", msg.tag)
		assert_equal (t, msg.type)
		assert_true (equals (data, msg.data))
	end
end

function test_respond()
	local msg = z.req ("test.response", {t = "this is the request"})

	assert_userdata (msg)
	assert_equal ("test.response", msg.tag)
	assert_equal ("request", msg.type)
	assert_equal ("this is the request", msg.data.t)

	local resp,err = msg:respond ({t = "this is the response"})
	if not resp then fail (err) end

	assert_userdata (resp)
	assert_equal ("test.response", resp.tag)
	assert_equal ("response", resp.type)

	assert_equal ("this is the response", resp.data.t)
end
