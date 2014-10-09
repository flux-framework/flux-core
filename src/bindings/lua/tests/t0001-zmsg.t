#!/usr/bin/lua

require 'Test.More'

local z = require 'zmsgtest'

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


plan 'no_plan'
ok (equals({},{}), "equals works on empty tables" )
nok (equals({}, {"foo"}), "equals detects unequal tables")


subtest ('Test zmsg basics', function ()
	local data = { one = "foo", two = "bar" }
	local msg = z.req ("test.tag", data)
	type_ok (msg, 'userdata', 'returned message is userdata')
	is (msg.tag, "test.tag",  'returned message tag is correct')
	is (msg.type, "request",  'returned message type is correct')
	ok  (equals (data, msg.data), 'returned message data is preserved')
end)

local ftypes = {
	request = z.req,
	response = z.resp,
	event = z.event,
	snoop = z.snoop
}

subtest ('Test zmsg types', function ()
	local data = {}
	for t,f in pairs (ftypes) do
		local msg,err = f ("test.tag", data)
		if not msg then fail ('function failed! ' ..err) end

		type_ok (msg,    'userdata', 'returned message type')
		is (msg.tag,     "test.tag", 'msg tag is "msg.tag"')
		is (msg.type, t, "msg.type is "..t)

		ok (equals (data, msg.data), 'msg.data preserved')
	end
end)

subtest ('Test zmsg response', function ()
	local msg = z.req ("test.response", {t = "this is the request"})

	type_ok (msg,   'userdata',            'type is userdata')
	is (msg.tag,    "test.response",       'msg.tag is correct')
	is (msg.type,   "request",             'msg.type is request')
	is (msg.data.t, "this is the request", 'msg.data is correct')

	local resp,err = msg:respond ({t = "this is the response"})
	if not resp then fail (err) end

	type_ok (resp,   'userdata',             'type is userdata')
	is (resp.tag,    "test.response",        'msg.tag is correct')
	is (resp.type,   "response",             'msg.type is response')
	is (resp.data.t, "this is the response", 'msg.data is correct')

end)

done_testing()
