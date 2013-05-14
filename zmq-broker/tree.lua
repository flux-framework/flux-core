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
-- compute n's jth child (0..k-1) in a k-ary tree, zero-origin
--

function P.k_ary_child (n, j, k)
    return k * n + j + 1
end

--
-- list n's children (comma-sep) in a k-ary tree of specified size, zero-origin
--

function P.k_ary_children (n, k, size)
    local i, c
    local s = ""
    for i=0, k-1 do
        c = P.k_ary_child (n, i, k)
        if (c < size) then
            if (i > 0) then s = s .. "," end
            s = s .. c
        end
    end
    return s
end

--
-- compute n's alternate parent in a k-ary tree, zero-origin
-- use the grandparent, except root - then use 1
--

function P.k_ary_parent2 (n, k)
    if tonumber (n) < 2 then return nil end
    local p = P.k_ary_parent (n, k)
    if (p == 0) then
        return 1
    end
    return P.k_ary_parent (p, k) 
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

-- FIXME: binomial_children

return tree
