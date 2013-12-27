local posix = require 'posix'

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
    ['ntasks']  = { char = 'n', arg = "N" },
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

function wreck:die (...)
    self:say (...)
    os.exit (1)
end

function wreck:usage()
    io.stderr:write ("Usage: "..self.prog.." OPTIONS.. COMMANDS..\n")
    io.stderr:write ([[
  -h, --help                 Display this message
  -n, --ntasks=N             Request to run a total of N tasks
  -o, --options=OPTION,...   Set other options (See OTHER OPTIONS below)
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

function wreck:parse_cmdline (arg)
    local getopt = require 'alt_getopt' .get_opts
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

    if self.optind > #arg then
        self:say ("Error: remote command required\n")
        self:usage()
        os.exit (1)
    end

    self.ntasks = tonumber (self.opts.n) or 1

    self.cmdline = {}
    for i = self.optind, #arg do
        table.insert (self.cmdline, arg[i])
    end

    return true
end

function wreck:jobreq ()
    if not self.opts then return nil, "Error: cmdline not parsed" end
    local ntasks = tonumber (self.opts.n)
    local jobreq = {
        ntasks =  tonumber (self.opts.n) or 1,
        cmdline = self.cmdline,
        environ = get_filtered_env (),
        cwd =     posix.getcwd (),
    }
    if self.opts.o then
        for opt in self.opts.o:gmatch ('[^,]+') do
            jobreq['options.'..opt] = 1
        end
    end
    return jobreq
end

function wreck.new (prog)
    local w = setmetatable ({extra_options = {}}, wreck) 
    w.prog = prog
    return w
end

return wreck

-- vi: ts=4 sw=4 expandtab
