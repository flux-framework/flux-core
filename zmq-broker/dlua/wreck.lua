#!/usr/bin/lua

local posix = require 'posix'
local flux = require 'flux'

-------------------------------------------------------------------------------
-- Local functions:
-------------------------------------------------------------------------------
local function usage ()
    io.stderr:write ("Usage: wreck [-n nprocs] COMMANDS...\n")
end
local function log_msg (...)
    io.stderr:write ("wreck: " .. string.format (...))
end

local function log_fatal (...)
    log_msg (...)
    os.exit (1)
end

--
--  Check that parameter [a] is an integer
--
local function is_integer (a)
    local b = tonumber (a)
    return (type (b) == "number") and (math.floor(b) == b)
end

---
--  Get the LWJ return code as highest of all task return codes
---
local function lwj_return_code (f, id)
    local hostlist = require 'hostlist'
    local lwj = f:kvsdir ("lwj.%d", id)
    local max = 0
    local msgs = {}
    for taskid in lwj:keys () do
        if is_integer (taskid) then
            local t = lwj[taskid]
            local x = t.exit_status
            if x > 0 then
                local s = "exited with " ..
                          (t.exit_code and "exit code" or "signal") ..
                          " %d\n"
                s = s:format(t.exit_code or t.exit_sig)
                if not msgs[s] then
                    msgs[s] = hostlist.new(taskid)
                else
                    msgs[s]:concat (taskid)
                end
            end
            if x > max then
                max = x
            end
        end
    end
    for s,h in pairs (msgs) do
        log_msg ("tasks %s: %s\n", tostring (h:sort()), s)
    end
    return max
end

--
-- Get the current env with some env vars filtered out:
--
local function get_filtered_env ()
    local env = posix.getenv()
    for k,v in pairs (env) do
        if k:match ("SLURM_") then env[k] = nil end
    end
    return (env)
end

-------------------------------------------------------------------------------
-- Main program:
-------------------------------------------------------------------------------
--  Parse cmdline args:
--
if #arg < 1 then
    usage ()
    os.exit (1)
end

local nprocs = 1
local cmdidx = 1

if arg[1]:sub (1,2) == "-n" then
    local s = table.remove (arg, 1)
    if s:len() == 2 then
        nprocs = tonumber (table.remove (arg, 1))
    else
        nprocs = tonumber (s:sub(3))
    end
end

--  Create new connection to local cmbd:
--
local f, err = flux.new()
if not f then log_fatal ("%s\n", err) end

--
--  Create a job request as Lua table:
--
local jobreq = {
      nprocs  = nprocs,
      cmdline = { unpack(arg) },
      environ = get_filtered_env(),
      cwd = posix.getcwd(),
}

log_msg ("Starting %d procs per node running \"%s\"\n",
    nprocs, table.concat (arg, ' '))

--
--  Send job request message with tag="job.create"
--
local resp, err = f:rpc ('job.create', jobreq)
if not resp then log_fatal ("%s\n", err) end

if resp.errnum then
    log_fatal ("job.create message failed with errnum=%d\n", resp.errnum)
end

log_msg ("Registered jobid %d\n", resp.jobid)

--
--  Get a handle to this lwj kvsdir:
--
local lwj, err = f:kvsdir ("lwj.%d", resp.jobid)
if not lwj then log_fatal ("%s\n", err) end

--
--  Send event to run the job
--
local rc,err = f:sendevent ("event.rexec.run.%d", resp.jobid)
if not rc then log_fatal ("%s\n", err) end

repeat
 local r = lwj:watch ("state", r)
 log_msg ("State = %s\n", r)
until r == "complete"

local rc = lwj_return_code (f, resp.jobid)
if rc == 0 then
    log_msg ("All tasks completed successfully.\n");
end


-- vi: ts=4 sw=4 expandtab
