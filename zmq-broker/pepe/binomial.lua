package.path = "liblua/?.lua;" .. package.path

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
local upreqinuri = "tcp://*:5556"
local dnreqouturi = "tcp://*:5557"
local child_opt = tree.binomial_children (pepe.rank, 2, #h)
if string.len (child_opt) > 0 then
    child_opt = " --children=" .. child_opt
end

if pepe.rank == 0 then
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --up-event-uri='" .. eventuri .. "'"
		.. " --up-req-in-uri='" .. upreqinuri .. "'"
		.. " --dn-req-out-uri='" .. dnreqouturi .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. " --set-conf-hostlist=" .. pepe.nodelist
                .. " --set-conf sync.period.sec=1.5"
                .. " --set-conf kvs.redis.hostname=\"127.0.0.1\""
                .. " --set-conf kvs.redis.port=6379"
                .. " --set-conf log.reduction.timeout.msec=100"
                .. " --set-conf log.circular.buffer.entries=100000"
                .. " --set-conf log.persist.priority=notice"
                .. " --set-conf live.missed.trigger.allow=5"
		.. " --plugins=api,barrier,live,log,conf,kvs,sync"
		.. " --logdest cmbd.log"
		.. child_opt)
else
    local parent_rank = tree.binomial_parent (pepe.rank, #h)
    local u1 = "tcp://" ..  h[parent_rank + 1] .. ":5556"
    local u2 = "tcp://" ..  h[parent_rank + 1] .. ":5557"
    pepe.run ("./cmbd --up-event-uri='" .. eventuri .. "'"
		.. " --up-req-in-uri='" .. upreqinuri .. "'"
		.. " --dn-req-out-uri='" .. dnreqouturi .. "'"
		.. " --parent='" .. parent_rank .. "," .. u1 .. "," .. u2 .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. " --plugins=api,barrier,live,log,conf"
		.. child_opt)
end
