local hostlist = require ("hostlist")
local tree = require ("tree")

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
local child_opt = tree.k_ary_children (pepe.rank, 3, #h)
if string.len (child_opt) > 0 then
    child_opt = " --children=" .. child_opt
end

if pepe.rank == 0 then
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. child_opt)
elseif pepe.rank == 1 then
    local p = tree.k_ary_parent (pepe.rank, 3)
    local parent = p .. ",tcp://" ..  h[p + 1] .. ":5556"
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --parent='" .. parent .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. child_opt)
else
    local p = tree.k_ary_parent (pepe.rank, 3)
    local p2 = tree.k_ary_parent2 (pepe.rank, 3)
    local parent = p .. ",tcp://" ..  h[p + 1] .. ":5556"
    local parent2 = p2 .. ",tcp://" ..  h[p2 + 1] .. ":5556"
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --parent='" .. parent .. "'"
		.. " --parent='" .. parent2 .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. child_opt)
end
