#!/usr/bin/lua

-- usage: treedot.lua hostlist flat|degenerate|binary|trinary|binomial 

local hostlist = require ("hostlist")
local h = hostlist.new (arg[1])
local size = hostlist.count (h)

function k_ary_parent (n, k)
    return math.floor ((n - 1)/k)
end

function binomial_parent (n, ranks)
    local parent = -1
    local dist = 1
    while (dist < ranks) do
        local src = n - dist
        if src >= 0 and src < dist then
            parent = n - dist
            if (parent < 0) then
                parent = parent + ranks
            end
        end
        dist = dist * 2
    end
    return parent
end

print ("graph cmbtree {")
for rank = 0,size - 1, 1 do
    local parent_rank
    if arg[2] == "binomial" then
	parent_rank = binomial_parent (rank, size)
    elseif arg[2] == "binary" then
	parent_rank = k_ary_parent (rank, 2)
    elseif arg[2] == "trinary" then
	parent_rank = k_ary_parent (rank, 3)
    elseif arg[2] == "degenerate" then
        parent_rank = rank - 1
    elseif arg[2] == "flat" then
        parent_rank = 0
    end

    if rank > 0 then
        print (h[parent_rank + 1] .. " -- " .. h[rank + 1])
    end
end
print ("}")
