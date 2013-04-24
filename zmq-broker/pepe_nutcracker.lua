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

function k_ary_parent (n, k)
    local p = (n - 1)/k
    return (p - p%1)
end

function make_nutcracker_yml (hl)
    local i
    local text = "beta:\n" ..
		 "  listen: 127.0.0.1:6379\n" ..
		 "  hash: fnv1a_64\n" ..
		 "  hash_tag: \"{}\"\n" ..
		 "  distribution: ketama\n" ..
		 "  auto_eject_hosts: false\n" ..
		 "  redis: true\n" ..
		 "  preconnect: true\n" ..
		 "  servers:\n"
    for i=1, #hl do
        text = text .. "   - " .. hl[i] ..
		":7777:1 server" .. tostring (i-1) .. "\n"
    end
    local filename = os.tmpname ()
    local fh = assert (io.open (filename, 'w'))
    fh:write (text)
    fh:close ()
    return filename
end

local h = hostlist.new (pepe.nodelist)
local eventuri = "epgm://eth0;239.192.1.1:5555"
local treeinuri = "tcp://*:5556"
local nutconf = make_nutcracker_yml (h)

pepe.run ("echo port 7777 | /usr/sbin/redis-server -")
pepe.run ("../../twemproxy-0.2.3/src/nutcracker -c " .. nutconf)

if pepe.rank == 0 then
   pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
else
   local parent_rank = k_ary_parent (pepe.rank, 3)
   local treeouturi = "tcp://" ..  h[parent_rank + 1] .. ":5556"
   pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --tree-out-uri='" .. treeouturi .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h)
end
