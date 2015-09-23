lua-hostlist -- lua bindings for manipulating lists of hosts
============================================================

lua-hostlist is a small set of lua bindings for the ~hostlist~ code
used in other LLNL projects such as [pdsh](http://pdsh.googlecode.com)
and [SLURM](http://www.llnl.gov/linux/slurm). It gives object
based access to manipulating hostlists directly from lua.

A proof-of-concept command line utility `hostlist` is provided as
a demonstration of the lua-hostlist bindings as well as a utility
for manipulating hostlist strings from the shell.

At this time, lua-hostlist is compatible with lua 5.1.

Installation
------------

In most cases a simple

```sh
 make && make install
```

should work. This will result in installation of the Lua module
`hostlist.so` into /usr/local/lib. The location of installation can
be controlled with `PREFIX` `LIBDIR` and `LUA_VER`, e.g.

```sh
 PREFIX=/usr LIBDIR=/usr/lib64 LUA_VER=5.1 make install
```

will install into `/usr/lib64/lua/5.1`.

The `hostlist` Utility
----------------------

The `hostlist` utility provided by lua-hostlist is a proof-of-concept
for the lua bindings, but may also be found to be a useful tool in
its own right. By default `hostlist` concatenates all hosts given
either on the commandline or via STDIN:

```sh
 $ hostlist foo[1-4] foo[5-10]
 foo[1-10]
```

Other operations such as count, union, intersection, exclusive or, etc.
are supported:

```
 Usage: hostlist [OPTION]... [HOSTLIST]...

  -h, --help                   Display this message.
  -q, --quiet                  Quiet output (exit non-zero if empty hostlist).
  -d, --delimiters=S           Set output delimiter (default = ",")
  -c, --count                  Print the number of hosts
  -s, --size=N                 Output at most N hosts (-N for last N hosts)
  -e, --expand                 Expand host list instead of collapsing
  -n, --nth=N                  Output the host at index N (-N to index from end)
  -u, --union                  Union of all HOSTLIST arguments
  -m, --minus                  Subtract all HOSTLIST args from first HOSTLIST
  -i, --intersection           Intersection of all HOSTLIST args
  -x, --exclude                Exclude all HOSTLIST args from first HOSTLIST
  -X, --xor                    Symmetric difference of all HOSTLIST args
  -R, --remove=N               Remove only N occurrences of args from HOSTLIST
  -S, --sort                   Return sorted HOSTLIST
  -f, --filter=CODE            Map Lua CODE over all hosts in result HOSTLIST
  -F, --find=HOST              Output position of HOST in result HOSTLIST
                                (exits non-zero if host not found)
```

 An arbitrary number of HOSTLIST arguments are supported for all
  operations.  The default operation is to concatenate all HOSTLIST args.


The lua-hostlist API
--------------------

Supported functions and methods exported from the hostlist library:

 * Create new hostlist

```lua
local hl, err = hostlist.new ()            -- Empty hostlist
local hl, err = hostlist.new (s1)          -- Hostlist from string s1
local hl, err = hostlist.new (s1, s2, s3)  -- Multiple args supported
```

```sh
$ hostlist  foo1 foo2 foo3 
foo[1-3]
$ echo foo1 foo2 foo3 | hostlist
foo[1-3]
```
 * Expand a "compressed" hostlist

```lua
-- Expand a hostlist object into a table:
local t = hostlist.expand (s)  -- Expand string 's'
local t = hl:expand()          -- Expand hostlist object 'hl'
```

```sh
$ hostlist --expand foo[1-4]
foo1,foo2,foo3,foo4
$ hostlist --delimiters='\n' --expand foo[1-4]
foo1
foo2
foo3
foo4
```

* Print a hostlist in lua

```lua
local hl = hostlist.new ("foo[1-5]")
print (hl)                -- promoted to string by __tostring() method
print ("hostlist" .. hl)  -- promoted to string by __concat method

--  To print an "expanded" hostlist first expand to a table, then
--   use table.concat()
print (table.concat (hl:expand()))
```

 * Count hosts in a hostlist

```lua
local count = hostlist.count (s1) -- Count hosts in string s1
local count = #hl                 -- Return number of hosts in object hl
```

```sh
$ hostlist -c foo[0-3]
4
```

 * Return a host at a specific index in a hostlist:

```lua
local nth = hostlist.nth (s)      -- Return nth host in hostlist string s
local nth = hl[n]                 -- Same for hostlist object hl
```

```sh
$ hostlist --nth=4 foo[0-100]
foo3
```

 * Find a host in a hostlist

```lua
local n = hostlist.find (s, host) -- Find host 'host' in hostlist string 's'
local n = hl:find (host)          -- Find host 'host' in hostlist object 'hl'
```

```sh
$ hostlist --find=foo3 'foo[0-10]' 
4
$ hostlist --find=bar 'foo[0-10]'
$ echo $?
1
```

 * Delete (subtract) hosts

```lua
local result = hostlist.delete (s1, s2)  -- Delete hosts in s2 from s1
local result = hl1 - hl2                 -- Delete hoslist hl2 from hl1
local result = hl - "foo1"               -- Delete foo1 from hostlist hl
```

```sh
$ hostlist --minus foo[1-4] foo2
foo[1,3-4]
```

 * Union of hostlists

```lua
local result = hostlist.union (s1, s2, ...) -- Union _all_ args
local result = hl + hl2         -- Union of hostlist objects hl and hl2
local result = hl + "foo[1-4]"  -- Union of hostlist hl with a string
```

```sh
$ hostlist --union 'foo[1-5]' 'foo[4-10]'
foo[1-10]
```
 * Intersection of hostlists

```lua
local r = hostlist.intersect (s1, s2, ...)  -- Intersect of all args
local r = hl1 * hl2         -- Intersect of hostlist hl1 hl2
local r = hl * "foo[4-10]"  -- Intersect of hostlist hl and string
```

```sh
$ hostlist --intersection 'foo[1-5]' 'foo[4-10]'
foo[4-5]
```

 * XOR of hostlists

```lua
local r = hostlist.xor (s1, s2, ..)  -- XOR of all args
local r = hl1 ^ hl2                  -- XOR of two hostlist objects
local r = hl ^ "foo[4-10]"           -- XOR of hostlist hl and string
```

```sh
$ hostlist --xor 'foo[1-5]' 'foo[4-10]'
foo[1-3,6-10]
```

 * Iteration:

```lua
--  hostlist 'next' method returns an iterator for the hostlist:
for host in hl:next() do
  -- hosts remain in hostlist hl, take care not to modify hl during traversal
end

--  An alternate method is to convert to a table and use pairs()
for _,host in pairs (hl:expand()) do
  -- iterating over an exapanded table of 'hl' here, ok to modify 'hl'
end
```

 * Map a function across a set of hosts

```lua
hostlist.map ("foo[1-5]",
  function (s)
    -- Do something with host string in "s"
  end)
hl:map (function (s) ... end)

--  Also acts as a filter, e.g. return only hosts divisible by 10:
local hl = hostlist.new ("foo[1-100]")
local r = hl:map (function (s) (s:match ("[%d]+$")%10 == 0) and s end)
print (r)
-- Output: foo[10,20,30,40,50,60,70,80,90,100]
```

```sh
#  only return hosts divisible by 10:
hostlist --filter='(s:match ("[%d]+$")%10 == 0) and s' foo[1-100]
foo[10,20,30,40,50,60,70,80,90,100]

#  prepend a string:
$ hostlist --filter='"hosts"..s' [1-100]
hosts[1-100]
```
