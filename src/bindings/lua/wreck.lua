--[[--------------------------------------------------------------------------
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
 ---------------------------------------------------------------------------]]
local posix = require 'flux.posix'
local hostlist = require 'flux.hostlist'
local cpuset = require 'flux.affinity'.cpuset

local wreck = {}
wreck.__index = wreck;

local lwj_options = {
--    ['ntasks'] =                "Set total number of tasks to execute",
    ['commit-on-task-exit'] =   "Call kvs_commit for each task exit",
    ['stdio-delay-commit'] =    "Don't call kvs_commit for each line of output",
    ['stdio-commit-on-open'] =  "Commit to kvs on stdio open in each task",
    ['stdio-commit-on-close'] = "Commit to kvs on stdio close in each task",
    ['stop-children-in-exec'] = "Start tasks in STOPPED state for debugger",
    ['no-pmi-server'] =         "Do not start simple-pmi server",
    ['trace-pmi-server'] =      "Log simple-pmi server protocol exchange",
}

local default_opts = {
    ['help']    = { char = 'h'  },
    ['verbose'] = { char = 'v'  },
    ['ntasks']  = { char = 'n', arg = "N" },
    ['gpus']  = { char = 'g', arg = "g" },
    ['cores-per-task']  = { char = 'c', arg = "N" },
    ['nnodes']  = { char = 'N', arg = "N" },
    ['tasks-per-node']  =
                   { char = 't', arg = "N" },
    ['walltime'] = { char = "T", arg = "SECONDS" },
    ['output'] =   { char = "O", arg = "FILENAME" },
    ['error'] =    { char = "E", arg = "FILENAME" },
    ['input'] =    { char = "i", arg = "HOW" },
    ['label-io'] = { char = "l", },
    ['skip-env'] = { char = "S", },
    ['options'] = { char = 'o', arg = "OPTIONS.." },
}

local function opt_table (w)
    local o = {}
    for x,t in pairs (default_opts) do
        o[x] = t.char
    end
    for _,v in pairs (w.extra_options) do
        o[v.name] = v.char
    end
    return o
end

local function short_opts (w)
    local s = ""
    for x,t in pairs (default_opts) do
        s = s .. t.char .. (t.arg and ":" or "")
    end
    for _,v in pairs (w.extra_options) do
        s = s .. v.char .. (v.arg and ":" or "")
    end
    return s
end

function wreck:say (...)
    io.stderr:write (self.prog..": "..string.format (...))
end

function wreck:verbose (...)
    if self.opts.v then
        self:say (...)
    end
end

function wreck:die (...)
    self:say (...)
    os.exit (1)
end

function wreck:usage()
    io.stderr:write ("Usage: "..self.prog.." OPTIONS.. COMMANDS..\n")
    io.stderr:write ([[
  -h, --help                 Display this message
  -v, --verbose              Be verbose
  -n, --ntasks=N             Request to run a total of N tasks
  -c, --cores-per-task=N     Request N cores per task
  -N, --nnodes=N             Force number of nodes
  -t, --tasks-per-node=N     Force number of tasks per node
  -o, --options=OPTION,...   Set other options (See OTHER OPTIONS below)
  -T, --walltime=N[SUFFIX]   Set max job walltime to N seconds. Optional
                             suffix may be 's' for seconds (default), 'm'
                             for minutes, 'h' for hours or 'd' for days.
                             N may be an arbitrary floating point number,
                             but will be rounded up to nearest second.
  -O, --output=FILENAME      Duplicate stdout/stderr from tasks to a file or
                             files. FILENAME is optionally a mustache
                             template with keys such as id, cmd, taskid.
                             (e.g. --output=flux-{{id}}.out)
  -i, --input=HOW            Indicate how to deal with stdin for tasks. HOW
                             is a list of [src:]dst pairs separated by semi-
                             colon, where src is an optional src file
                             (default: stdin) and dst is a list of taskids
                             or "*" or "all". E.g. (-i0 sends stdin to task 0,
                             -i /dev/null:* closes all stdin, etc.)
  -E, --error=FILENAME       Send stderr to a different location than stdout.
  -l, --labelio              Prefix lines of output with task id
  -S, --skip-env             Skip export of environment to job
]])
    for _,v in pairs (self.extra_options) do
        local optstr = v.name .. (v.arg and "="..v.arg or "")
        io.stderr:write (
            string.format ("  -%s, --%-20s %s\n", v.char, optstr, v.usage))
    end
    io.stderr:write ("\nOTHER OPTIONS:\n")
    for o,d in pairs (lwj_options) do
        io.stderr:write (string.format ("  %-26s %s\n", o, d))
    end
end


function wreck.get_filtered_env ()
    local env = posix.getenv()
    env.HOSTNAME = nil
    env.ENVIRONMENT = nil
    for k,v in pairs (env) do
        if k:match ("SLURM_") then env[k] = nil end
        if k:match ("FLUX_URI") then env[k] = nil end
    end
    return (env)
end

local function get_job_env (arg)
    local f = arg.flux
    local env = wreck.get_filtered_env ()
    local default_env = {}
    if f then default_env = f:kvs_get ("lwj.environ") or {} end
    for k,v in pairs (env) do
        -- If same key=value is already in default env no need to
        --  export it again, remove:
        if default_env[k] == env[k] then env[k] = nil end
    end
    return (env)
end


local function job_kvspath (f, id)
    assert (id, "Required argument id missing!")
    local arg = { id }
    if type (id) == "table" then
        arg = id
    end
    local r, err = f:rpc ("job.kvspath", {ids = arg })
    if not r then error (err) end
    if type (id) == "table" then return r.paths end
    return r.paths [1]
end

---
-- Return kvs path to job id `id`
--
local kvs_paths = {}
local function kvs_path (f, id)
    if not kvs_paths[id] then
        kvs_paths [id] = job_kvspath (f, id)
    end
    return kvs_paths [id]
end

local function kvs_path_multi (f, ids)
    local result = job_kvspath (f, ids)
    for i,id in ipairs (ids) do
        kvs_paths [id] = result [id]
    end
    return result
end

function wreck:lwj_path (id)
    assert (id, "missing required argument `id'")
    if not self.lwj_paths[id] then
        self.lwj_paths [id] = job_kvspath (self.flux, id)
    end
    return self.lwj_paths [id]
end

function wreck:kvsdir (id)
    if not self.kvsdirs[id] then
        local f = self.flux
        local d, err = f:kvsdir (self:lwj_path (id))
        if not d then return nil, err end
        self.kvsdirs[id] = d
    end
    return self.kvsdirs[id]
end

function wreck:add_options (opts)
    for k,v in pairs (opts) do
        if not type (v) == "table" then return nil, "Invalid parameter" end

        local char = v.char
        if default_opts [v.name] then
            return nil, "Can't override option '"..k.."'"
        end
        table.insert (self.extra_options, v)
    end
    return true
end

function wreck:getopt (opt)
    if not self.opts then return nil  end
    return self.opts [opt]
end

local function parse_walltime (s)
    local m = { s = 1, m = 60, h = 3600, d = 56400 }
    local n, suffix = s:match ("^([0-9.]+)([HhMmDdSs]?)$")
    if not tonumber (n) then
        return nil, "Invalid duration '"..s.."'"
    end
    if suffix and m [suffix]  then
        n = (n * m [suffix])
    end
    return math.ceil (n)
end

function wreck:parse_cmdline (arg)
    local getopt = require 'flux.alt_getopt' .get_opts
    local s = short_opts (self)
    local v = opt_table (self)
    self.opts, self.optind = getopt (arg, s, v)

    if self.opts.h then
        self:usage()
        os.exit (0)
    end

    if self.opts.o then
        for opt in self.opts.o:gmatch ("[^,]+") do
            if not lwj_options [opt] then
                return nil, string.format ("Unknown LWJ option '%s'\n", opt)
            end
        end
    end

    if self.opts.T then
        self.walltime, err = parse_walltime (self.opts.T)
        if not self.walltime then
            self:die ("Error: %s", err)
        end
    end

    if self.optind > #arg then
        self:say ("Error: remote command required\n")
        self:usage()
        os.exit (1)
    end

    self.nnodes = self.opts.N and tonumber (self.opts.N)

    if not self.opts.t then
        self.opts.t = 1
    end

    -- If nnodes was provided but -n, --ntasks not set, then
    --  set ntasks to nnodes.
    if self.opts.N and not self.opts.n then
        self.ntasks = self.nnodes * self.opts.t
    else
        self.ntasks = self.opts.n and tonumber (self.opts.n) or 1
    end
    if self.opts.c then
        self.ncores = self.opts.c * self.ntasks
    else
        self.ncores = self.ntasks
    end

    self.gpus = 0
    if self.opts.g then
        self.gpus = self.opts.g
    end

    self.tasks_per_node = self.opts.t

    self.cmdline = {}
    for i = self.optind, #arg do
        table.insert (self.cmdline, arg[i])
    end

    return true
end

local function inputs_table_from_args (wreck, s)
    -- Default is to broadcast stdin to all tasks:
    if not s then return { { src = "stdin", dst = "*" } } end

    local inputs = {}
    for m in s:gmatch ("[^;]+") do
        local src,dst = m:match ("^([^:]-):?([^:]+)$")
        if not src or not dst then
            wreck:die ("Invalid argument to --input: %s\n", s)
        end
        --  A dst of "none" short circuits rest of entries
        if dst == "none" then return inputs end

        --  Verify the validity of dst
        if dst ~= "all" and dst ~= "*" then
            local h, err = cpuset.new (dst)
            if not h then
                if src ~= "" then
                    wreck:die ("--input: invalid dst: %s\n", dst)
                end
                -- otherwise, assume only dst was provided
                src = dst
                dst = "*"
            end
        end
        if src == "" or src == "-" then src = "stdin" end
        table.insert (inputs, { src = src, dst = dst })
    end
    -- Add fall through option
    table.insert (inputs, { src = "/dev/null", dst = "*" })
    return inputs
end

local function fixup_nnodes (wreck)
    if not wreck.flux then return end
    if not wreck.nnodes then
        wreck.nnodes = math.min (wreck.ntasks, wreck.flux.size)
    elseif wreck.nnodes > wreck.flux.size then
        wreck:die ("Requested nodes (%d) exceeds available (%d)\n",
                  wreck.nnodes, wreck.flux.size)
    end
end

function wreck:jobreq ()
    if not self.opts then return nil, "Error: cmdline not parsed" end
    if self.fixup_nnodes then
        fixup_nnodes (self)
    end
    local jobreq = {
        nnodes =  self.nnodes or 0,
        ntasks =  self.ntasks,
        ncores =  self.ncores,
        cmdline = self.cmdline,
        environ = self.opts.S and {} or get_job_env { flux = self.flux },
        cwd =     posix.getcwd (),
        walltime = self.walltime or 0,
        gpus = self.gpus or 0,

        ["opts.nnodes"] = self.opts.N,
        ["opts.ntasks"]  = self.opts.n,
        ["opts.cores-per-task"] = self.opts.c,
        ["opts.tasks-per-node"] = self.opts.t,
        ["options.stdio-delay-commit"] = 1,
    }
    if self.opts.o then
        for opt in self.opts.o:gmatch ('[^,]+') do
            jobreq['options.'..opt] = 1
        end
    end
    if self.opts.O or self.opts.E then
        jobreq.output = {
            files = {
              stdout = self.opts.O,
              stderr = self.opts.E,
            },
            labelio = self.opts.l,
        }
    end
    jobreq ["input.config"] = inputs_table_from_args (self, self.opts.i)
    return jobreq
end

local function send_job_request (self, name)
    if not self.flux then
        local f, err = require 'flux'.new ()
        if not f then return nil, err end
        self.flux = f
    end
    local jobreq, err = self:jobreq ()
    if not jobreq then
        return nil, err
    end
    local resp, err = self.flux:rpc (name, jobreq)
    if not resp then
        return nil, "flux.rpc: "..err
    end
    if resp.errnum then
        return nil, name.." message failed with errnum="..resp.errnum
    end
    return resp
end

function wreck:submit ()
    local resp, err = send_job_request (self, "job.submit")
    if not resp then return nil, err end
    if resp.state ~= "submitted" then
        return nil, "job submission failure, job left in state="..resp.state
    end
    return resp.jobid, resp.kvs_path
end

function wreck:createjob ()
    self.fixup_nnodes = true
    local resp, err = send_job_request (self, "job.create")
    if not resp then return nil, err end
    --
    -- If reply contains a kvs path to this LWJ, pre-memoize the id
    --  to kvs path mapping so we do not have to fetch it again.
    if resp.kvs_path then
        self.lwj_paths [resp.jobid] = resp.kvs_path
        kvs_paths [resp.jobid] = resp.kvs_path
    end
    return resp.jobid, resp.kvs_path
end

local function initialize_args (arg)
    if arg.ntasks and arg.nnodes then return true end
    local f = arg.flux
    local lwj, err = f:kvsdir (kvs_path (f, arg.jobid))
    if not lwj then
        return nil, "Error: "..err
    end
    arg.ntasks = lwj.ntasks
    arg.nnodes = lwj.nnodes
    return true
end

function wreck.ioattach (arg)
    local ioplex = require 'wreck.io'
    local f = arg.flux
    local rc, err = initialize_args (arg)
    if not rc then return nil, err end

    local taskio, err = ioplex.create (arg)
    if not taskio then return nil, err end
    for i = 0, arg.ntasks - 1 do
        for _,stream in pairs {"stdout", "stderr"} do
            local rc, err =  taskio:redirect (i, stream, stream)
        end
    end
    taskio:start (arg.flux)
    return taskio
end

local logstream = {}
logstream.__index = logstream

local function logstream_print_line (iow, line)
    io.stderr:write (string.format ("rank%d: Error: %s\n", iow.id, line))
end

function logstream:dump ()
    for _, iow in pairs (self.watchers) do
        local r, err = iow.kz:read ()
        while r and r.data and not r.eof do
           logstream_print_line (iow, r.data)
           r, err = iow.kz:read ()
        end
    end
end

function wreck.logstream (arg)
    local l = {}
    local f = arg.flux
    if not f then return nil, "flux argument member required" end
    local rc, err = initialize_args (arg)
    if not rc then return nil, err end
    if not arg.nnodes then return nil, "nnodes argument required" end
    l.watchers = {}
    for i = 0, arg.nnodes - 1 do
        local key = kvs_path (f, arg.jobid)..".log."..i
        local iow, err = f:iowatcher {
            key = key,
            kz_flags = arg.oneshot and { "nofollow" },
            handler = function (iow, r, err)
                if not r then
                    if err then
                        io.stderr:write ("logstream kz error "..err.."\n")
                    end
                    return
                end
                logstream_print_line (iow, r)
            end
        }
        iow.id = i
        table.insert (l.watchers, iow)
    end
    return setmetatable (l, logstream)
end

local function exit_message (t)
    local s = "exited with "
    s = s .. (t.exit_code and "exit code" or "signal") .. " %d"
    return s:format (t.exit_code or t.exit_sig)
end

local function task_status (lwj, taskid)
    if not tonumber (taskid) then return nil end
    local t = lwj[taskid]
    if not t.exit_status then
        return 0, (t.procdesc and "starting" or "running")
    end
    local x = t.exit_code or (t.exit_sig + 128)
    return x, exit_message (t)
end

local function exit_status_aggregate (arg)
    local flux = require 'flux'
    local f = arg.flux
    local lwj = arg.kvsdir
    local msgs = {}

    local exit_status = lwj.exit_status
    if not exit_status then return nil, nil end

    local s, max, core = flux.exitstatus (exit_status.max)
    if s == "killed" then max = max + 128 end

    for ids,v in pairs (exit_status.entries) do
        local msg, code, core = flux.exitstatus (v)
        s = "exited with "
        s = s .. (msg == "exited" and "exit code " or "signal ") .. code
        msgs [s] = hostlist.new (ids)
    end

    return max, msgs
end


--- Return highest exit code from all tasks and task exit message list.
-- Summarize job exit status for arg.jobid by returning max task exit code,
-- along with a list of task exit messages to be optionally emitted to stdout.
-- @param arg.jobid job identifier
-- @param arg.flux  (optional) flux handle
-- @return exit_cde, msg_list
function wreck.status (arg)
    local flux = require 'flux'
    local f = arg.flux
    local jobid = arg.jobid
    if not jobid then return nil, "required arg jobid" end

    if not f then f = require 'flux'.new() end
    local lwj = f:kvsdir (kvs_path (f, jobid))

    local max = 0
    local msgs = {}

    if lwj.exit_status then
        return exit_status_aggregate { flux = f, kvsdir = lwj }
    end

    for taskid in lwj:keys () do
        local x, s = task_status (lwj, taskid)
        if x then
            if x > max then max = x end
            if not msgs[s] then
                msgs[s] = hostlist.new (taskid)
            else
                msgs[s]:concat (taskid)
            end
        end
    end
    return max, msgs
end

function wreck.id_to_path (arg)
    local flux = require 'flux'
    local f = arg.flux
    local id = arg.jobid
    if not id then return nil, "required arg jobid" end
    if not f then f, err = require 'flux'.new () end
    if not f then return nil, err end
    return kvs_path (f, id)
end

local function kvsdir_reverse_keys (dir)
    local result = {}
    for k in dir:keys () do
        if tonumber (k) then
            table.insert (result, k)
        end
    end
    table.sort (result, function (a,b) return tonumber(b) < tonumber(a) end)
    return result
end

local function reverse (t)
    local len = #t
    local r = {}
    for i = len, 1, -1 do
        table.insert (r, t[i])
    end
    return r
end

function wreck.jobids_to_kvspath (arg)
    local f = arg.flux
    local ids = arg.jobids
    if not ids then return nil, 'missing required arg jobids' end
    if not f then f, err = require 'flux'.new () end
    if not f then return nil, err end
    return kvs_path_multi (f, ids)
end

function wreck.joblist (arg)
    local flux = require 'flux'
    local f = arg.flux
    if not f then f, err = flux.new () end
    if not f then return nil, err end

    local function visit (d, r)
        local results = r or {}
        if not d then return end
        local dirs = kvsdir_reverse_keys (d)
        for _,k in pairs (dirs) do
            local path = tostring (d) .. "." .. k
            local dir = f:kvsdir (path)
            if dir then
                if dir.state then
                    -- This is a lwj dir, add to results table:
                    table.insert (results, path)
                else
                    -- recurse to find lwj dirs lower in the directory tree
                    visit (dir, results)
                end
		if arg.max and #results >= arg.max then return results end
            end
        end
        return results
    end

    local dir, err = f:kvsdir ("lwj")
    if not dir then
        if err:match ("No such") then err = "No job information in KVS" end
        return nil, err
    end

    return reverse (visit (dir))
end

local function shortprog ()
    local prog = string.match (arg[0], "([^/]+)$")
    return prog:match ("flux%-(.+)$")
end

function wreck.new (arg)
    if not arg then arg = {} end
    local w = setmetatable ({ extra_options = {},
                              kvsdirs = {},
                              lwj_paths = {}}, wreck)
    w.prog = arg.prog or shortprog ()
    w.flux = arg.flux
    return w
end

return wreck

-- vi: ts=4 sw=4 expandtab
