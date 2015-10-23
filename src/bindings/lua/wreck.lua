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
}

local default_opts = {
    ['help']    = { char = 'h'  },
    ['verbose'] = { char = 'v'  },
    ['ntasks']  = { char = 'n', arg = "N" },
    ['walltime'] = { char = "T", arg = "SECONDS" },
    ['output'] =   { char = "O", arg = "FILENAME" },
    ['error'] =    { char = "E", arg = "FILENAME" },
    ['input'] =    { char = "i", arg = "HOW" },
    ['label-io'] = { char = "l", },
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
                             or "*" or "all". E.g. (-s0 sends stdin to task 0,
                             -s /dev/null:* closes all stdin, etc.)
  -E, --error=FILENAME       Send stderr to a different location than stdout.
  -l, --labelio              Prefix lines of output with task id
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

local function get_filtered_env ()
    local env = posix.getenv()
    env.HOSTNAME = nil
    env.ENVIRONMENT = nil
    for k,v in pairs (env) do
        if k:match ("SLURM_") then env[k] = nil end
        if k:match ("FLUX_API") then env[k] = nil end
        if k:match ("FLUX_URI") then env[k] = nil end
    end
    -- XXX: MVAPICH2 at least requires MPIRUN_RSH_LAUNCH to be set
    --  in the environment or PMI doesn't work (for unknown reason)
    env.MPIRUN_RSH_LAUNCH = 1
    return (env)
end

function wreck:add_options (opts)
    for _,v in pairs (opts) do
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

    self.nnodes = tonumber (self.opts.N) or 1
    self.ntasks = tonumber (self.opts.n) or 1

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

function wreck:jobreq ()
    if not self.opts then return nil, "Error: cmdline not parsed" end
    local nnodes = tonumber (self.opts.N)
    local ntasks = tonumber (self.opts.n)
    local jobreq = {
        nnodes =  tonumber (self.opts.N) or 1,
        ntasks =  tonumber (self.opts.n) or 1,
        cmdline = self.cmdline,
        environ = get_filtered_env (),
        cwd =     posix.getcwd (),
        walltime =self.walltime or 0
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

local function initialize_args (arg)
    if arg.ntasks and arg.nnodes then return true end
    local f = arg.flux
    local lwj, err = f:kvsdir ("lwj."..arg.jobid)
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

function logstream:dump ()
    for _, iow in pairs (self.watchers) do
        local r, err = iow.kz:read ()
        while r and r.data and not r.eof do
           io.stderr:write (r.data.."\n")
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
    l.watchers = {}
    for i = 0, arg.nnodes - 1 do
        local key ="lwj."..arg.jobid..".log."..i
        local iow, err = f:iowatcher {
            key = key,
            handler = function (iow, r)
                if not r then return end
                io.stderr:write (r.."\n")
            end
        }
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

--- Return highest exit code from all tasks and task exit message list.
-- Summarize job exit status for arg.jobid by returning max task exit code,
-- along with a list of task exit messages to be optionally emitted to stdout.
-- @param arg.jobid job identifier
-- @param arg.flux  (optional) flux handle
-- @return exit_cde, msg_list
function wreck.status (arg)
    local f = arg.flux
    local jobid = arg.jobid
    if not jobid then return nil, "required arg jobid" end

    if not f then f = require 'flux'.new() end
    local lwj = f:kvsdir ("lwj."..jobid)
    local max = 0
    local msgs = {}
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

function wreck.new (prog)
    local w = setmetatable ({extra_options = {}}, wreck) 
    w.prog = prog
    return w
end

return wreck

-- vi: ts=4 sw=4 expandtab
