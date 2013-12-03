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
    local lwj = f:kvsdir ("lwj.%d", id)
    local max = 0
    for taskid in lwj:keys () do
        if is_integer (taskid) then
            local x = lwj[taskid].exit_status
            if x > 0 then
                log_msg ("task %d exited with exit code %d\n", taskid, x)
            end
            if x > max then
                max = x
            end
        end
    end
    return max
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
        nprocs = tostring (table.remove (arg, 2))
    else
        nprocs = tostring (s:sub(3))
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
