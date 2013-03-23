--###########################################################################
--
--  Initalize a single redis-server instance as part of a job
--
--   Set redis.server and redis.cli below until these are configurable
--    on the command line. For now
--###########################################################################
--
--
-- Script configuration:
--
local redis = {
    -- Path to redis-server
    server = "/usr/bin/redis-server",
    -- Path to redis-client (used to terminate redis-server)
    cli    = "/usr/bin/redis-cli",
    nodeid = -2,
    opt =
    {
     name =    "redis",
     usage =   "Invoke redis-server as daemon from srun",
     arginfo = "NODEID",

     -- Argument is optional: --redis implies --redis=-1
     has_arg = 2,
     val     = 1,
    },
}


---
--  Initialize spank plugin: register the --redis option in srun
---
function slurm_spank_init (spank)
    if spank.context == "remote" then
        return SPANK.SUCCESS
    end

    spank:register_option (redis.opt)
end

---
--  This is where we invoke redis-server and send its configuration
--  XXX: Remote redis-server invocation through sub-srun invocation
--    does not work yet.
---
function slurm_spank_local_user_init (spank)

    local optarg = spank:getopt (redis.opt)

    if optarg then
        if type(optarg) == "boolean"  then
            redis.nodeid = -1
        elseif tonumber(optarg) then
            redis.nodeid = tonumber(optarg)
        else
            SPANK.log_error ("--redis: argument '%s' invalid.", optarg)
            return SPANK.FAILURE
        end
    end

    local nnodes = spank:get_item ('S_JOB_NNODES')

    if redis.nodeid >= nnodes then
        SPANK.log_error ("--redis: argument '%s' invalid.", optarg)
        return SPANK.FAILURE
    end

    spank:setenv ("REDIS_NODEID", redis.nodeid, 1)

    return spawn_redis_daemon()
end

---
--  Spawn the redis-server as a daemon
---
function spawn_redis_daemon ()
    local cmd
    local daemonize = true
    local posix = require 'posix'

    if redis.nodeid > -1 then
        cmd = "srun --output=/tmp/redis-%J.log -N1 -n1 -r " .. redis.nodeid .. " "
              .. redis.server .. " -"
        daemonize = false
    else
        cmd  = redis.server .. " -"
    end

    SPANK.log_info ("Going to run %s", cmd)
    local f, err = io.popen (cmd, "w")
    if not f then
        SPANK.log_error ("redis: Failed to execute '%s': %s", cmd, err)
        return SPANK.FAILURE
    end
    f:write ("loglevel  debug\n")
    f:write ("daemonize yes\n")
    f:write ("pidfile   /tmp/redis.pid\n")
    f:write ("logfile   /tmp/redis.log\n")
    f:close()

    return SPANK.SUCCESS
end

---
--  At exit, we tell the redis-server to shutdown
--  XXX: Only works for running on the same host as srun for now
---
function slurm_spank_exit (spank)
    if spank.context == "local" then
        os.execute (redis.cli .. " shutdown")
    end
end

-- vi: ts=4 sw=4 expandtab
