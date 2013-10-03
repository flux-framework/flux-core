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
local mport = 5000 + tonumber (pepe:getenv ("SLURM_JOB_ID")) % 1024;
local eventuri = "epgm://eth0;239.192.1.1:" .. tostring (mport)
local upreqinuri = "tcp://*:5556"
local dnreqouturi = "tcp://*:5557"

if pepe.rank == 0 then
    local topology = tree.k_ary_json (2, #h)
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --up-event-uri='" .. eventuri .. "'"
		.. " --up-req-in-uri='" .. upreqinuri .. "'"
		.. " --dn-req-out-uri='" .. dnreqouturi .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. " --set-conf-hostlist=" .. pepe.nodelist
		.. " --set-conf conf.sync.period-sec=1.5"
		.. " --set-conf conf.log.reduction-timeout-msec=100"
		.. " --set-conf conf.log.circular-buffer-entries=100000"
		.. " --set-conf conf.log.persist-level=debug"
		.. " --set-conf conf.live.missed-trigger-allow=5"
		.. " --set-conf conf.live.topology='" .. topology .. "'"
		.. " --plugins=api,barrier,live,log,kvs,sync,job,rexec"
		.. " --logdest cmbd.log")
else
    local parent_rank = tree.k_ary_parent (pepe.rank, 2)
    local u1 = "tcp://" ..  h[parent_rank + 1] .. ":5556"
    local u2 = "tcp://" ..  h[parent_rank + 1] .. ":5557"
    pepe.run ("./cmbd --up-event-uri='" .. eventuri .. "'"
		.. " --up-req-in-uri='" .. upreqinuri .. "'"
		.. " --dn-req-out-uri='" .. dnreqouturi .. "'"
		.. " --parent='" .. parent_rank .. "," .. u1 .. "," .. u2 .. "'"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. " --plugins=api,barrier,live,log,kvs,job,rexec")
end
