#!/usr/bin/lua
local tree = require ("tree")

local k=8
local size=256

local p = tree.k_ary_parent (arg[1], k)
if p then
    print ("k_ary_parent:   " .. p)
else
    print ("k_ary_parent:   none")
end

local c = tree.k_ary_children (arg[1], k, size)
print ("k_ary_children: " .. c)

local child_opt = tree.k_ary_children (arg[1], k, size)
if string.len (child_opt) > 0 then
    child_opt = " --children=" .. child_opt
end

print ("child_opt: " .. child_opt)
