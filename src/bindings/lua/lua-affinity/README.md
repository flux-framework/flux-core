[![Build Status](https://travis-ci.org/grondo/lua-affinity.svg?branch=master)](https://travis-ci.org/grondo/lua-affinity)
[![Coverage Status](https://coveralls.io/repos/grondo/lua-affinity/badge.svg?branch=master&service=github)](https://coveralls.io/github/grondo/lua-affinity?branch=master)

# lua-affinity 

The lua-affinity module for Lua is a thin wrapper around Linux
schedutils calls `sched_setaffinity(2)` and `sched_getaffinity(2)`.
It also contains a Lua interface for creating and manipulating
the `cpu_set_t` CPU masks used by the interface.

## Examples

Creating `cpu_set_t` objects:

    local cpu_set =  require 'affinity.cpuset'
    c = cpu_set.new("0-1,4")              -- create cpu_set from cstr list
    x = cpu_set.new("0xf0")               -- create a cpu_set from hex strin
    v = cpu_set.new()                     -- Empty cpu_set
    m = cpu_set.new("00000000,0000000f")  -- Linux bit string format

## `cpu_set_t` Methods:

Operators:

 * Equals: `s1 == s2`: test for `s1` equals `s2` (same CPUs in set)
 * Addition: `s1 + s1`: Return the union of `s1` and `s2` (returns copy)
 * Deletion: (`s1 - s2`): Unset CPUs in `s2` from `s1`
 * Length: (`#s1`): Return number of CPUs set in cpuset
 * Tostring: (`tostring (s1)`): Convert s1 to cstr type CPU list (0-1,4)
 * Concatenation (`..`): string concatenation as above (implied tostring)

Named methods:

 * `s:set (i,...)`: Explicitly set CPU or CPUs
 * `s:clr (i,...)`: Explicitly clear CPU or CPUs
 * `s:isset (i)`: Return true if CPU`i` set in `s`
 * `s:zero ()`: Clear all cpus from set s
 * `s:first ()`: Return first set CPU in `s`
 * `s:last ()`: Return last set CPU in `s`
 * `s:count ()`: Return number of CPUs in the set
 * `s:weight ()`: Return number of CPUs in the set
 * `s:tohex ()`: Return hex string representation of `s` (linux bitstring format)
 * `s:union (s2)`: Set all bits in `s1` that are set in `s1` or `s2`
 * `s:intersect (s2)`: unset bits in s1 that are not set in `s2`
 * `s:is_in (s2)`: Is set `s` contained within `s2`
 * `s:contains (s2)`: Is set `s2` contained within set `s`
 * `s:expand ([fn])`: Expand `s` into a table, optionally using function `fn` as a filter.

Iterator:

 * `s:setbits ()`: Iterate over all set bits in `s`:

        s = cpuset.new ("0xf001")
        for i in s:setbits () do
            print (i)
        end

## `cpu_set_t` Indexing:

 * `s[i]`: Return true if CPU `i` is set in cpu_set otherwise false
 * `s[i] = 0`: Unset CPU `i` in `s`
 * `s[i] = 1`: Set CPU `i` in `s`


## getaffinity and setaffinity

    local affinity = require 'affinity'
    local s1 = affinity.getaffinity ()
    print (s1)
    local s2 = affinity.cpuset.new ("0-1")
    local rc, err = affinity.setaffinity (s2)


