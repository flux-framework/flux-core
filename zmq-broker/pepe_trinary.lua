local hostlist = require ("hostlist")

if pepe.rank == 0 then
    local env = pepe:getenv()
    local v,err = pepe:getenv("ENV")
    if not v then
	print ("getenv(ENV): " .. err .. "\n")
    end
    pepe:unsetenv ("ENV")
    pepe:setenv ("HAVE_PEPE", 1)
    pepe:setenv ("PS1", "${SLURM_JOB_NODELIST} \\\u@\\\h \\\w$ ")

end

local h = hostlist.new (pepe.nodelist)
local eventuri = "epgm://eth0;239.192.1.1:5555"
local treeinuri = "tcp://*:5556"

-- N.B. results undefined for n = 0
function k_ary_parent (n, k)
    return math.floor ((n - 1)/k)
end

-- strategy: adoption by grandparent
-- if root (n = 0) dies, 1 is the new root
-- N.B. results undefined for n = 0,1
function k_ary_parent2 (n, k)
    local p = k_ary_parent (n, k)
    if (p == 0) then
        return 1
    end
    return k_ary_parent (p, k) 
end

if pepe.rank == 0 then
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
elseif pepe.rank == 1 then
    local p = k_ary_parent (pepe.rank, 3)
    local parent = p .. ",tcp://" ..  h[p + 1] .. ":5556"
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --parent='" .. parent .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
else
    local p = k_ary_parent (pepe.rank, 3)
    local p2 = k_ary_parent2 (pepe.rank, 3)
    local parent = p .. ",tcp://" ..  h[p + 1] .. ":5556"
    local parent2 = p2 .. ",tcp://" ..  h[p2 + 1] .. ":5556"
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --parent='" .. parent .. "'"
		.. " --parent='" .. parent2 .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
end
