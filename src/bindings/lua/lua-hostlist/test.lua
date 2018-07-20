#!/usr/bin/env lua
require "hostlist"

function printf (...)
	print (string.format (...))
end

if #arg ~= 1 then
   printf ("Usage: %s <hostlist>", arg[0])
   os.exit(1)
end

-- hostlist Lua interface
-- Create new hostlist with hostlist.new() 

h = hostlist.new (arg[1])

printf ("Hostlist `%s' has %d hosts\n", tostring (h), #h);
printf ("Concatenation: %s", "hostlist is " .. h)
printf ("add hosts with + operator, but this creates a copy: %s\n",
		tostring (h + "host77"))

h2 = hostlist.new ("foo[1-5]")
printf ("Remove hosts with - operator (%s - %s = %s)",
		h2.."", "foo3", tostring (h2 - "foo3"))

printf ("Access nth host with [] operator: h[%d] = %s", 1, h[1])
printf ("Access from end [] operator: h[%d] = %s", -1, h[-1])

-- Other methods
h:concat ("foo100")
h:delete ("foo100")
h:sort()
h:uniq()

-- Iterator
for host in h:next() do
	print (host)
end

-- The expand function: Turn a hostlist into a table

t = h:expand()

printf ("h:map(): result has %d members", #t)

-- Or call a function for each host

t = h:expand(print)

printf ("h:map(print): result has %d members",  #t)

--
-- map also acts a filter function. return nil to drop entry
--
l = hostlist.new ("a[1-5],b,a6")
printf ("l = %s (%d hosts)", l.."", #l)

-- Only return hosts that end in a digit
t = l:expand (function(s) return s:match("%d$") and s  end)

printf ("l:map(fn): %s (%d members)",
		""..hostlist.new(table.concat(t, ",")), #t)


-- intersection

l = hostlist.new ("h[1-10]")
print ("r = " .. l * "h[9-12]")

-- pop hosts off list

t = l:pop(4)
h = hostlist.new (table.concat (t, ","))
h:uniq()

printf ("Popped %s off list %s\n", ""..h, ""..l)


