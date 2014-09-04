local P = {}
tree = P

--
-- compute n's parent in a k-ary tree, zero-origin
--

function P.k_ary_parent (n, k)
    if tonumber (n) < 1 then return nil end
    return math.floor ((n - 1)/k)
end

--
-- Generate JSON 2-dim array representing topology for k-ary tree.
-- 

function P.k_ary_json (k, size)
    local i, j
    local s = "["
    for i=1, size-1, k do
        s = s .. "["
        for j=0, k-1 do
            s = s .. tostring (i + j)
            if i + j == size - 1 then break end
            if j < k-1 then s = s .. "," end
        end
        s = s .. "],"
    end
    s = s .. "]"
   return s
end
	
--
-- compute n's parent in a binomial tree of specified size, zero-origin
--

function P.binomial_parent (n, size)
    local parent = -1
    local dist = 1
    while (dist < size) do
        local src = n - dist
        if src >= 0 and src < dist then
            parent = n - dist
            if (parent < 0) then
                parent = parent + size 
            end
        end
        dist = dist * 2
    end
    return parent
end

return tree
