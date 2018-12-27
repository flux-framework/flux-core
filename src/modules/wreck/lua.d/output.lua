--
--  Optionally write job output to file(s) if so configured in either
--   lwj.output or lwj.<id>.output
--
--  Output filenames are rendered mustache templates
--
--
local ioplex = require 'wreck.io'
local flux = require 'flux'

local function kvs_output_config (wreck)
    local o = wreck.kvsdir ["output"]
    if not o then
       o = wreck.flux:kvsdir ("lwj.output")
    end
    return o
end

local function render (template, wreck, taskid)
    local lustache = require 'flux.lustache'
    local view = {
        cmd = wreck.argv[0]:match("([^/]+)$"),
        taskid = taskid
    }
    setmetatable (view, { __index = wreck })
    return lustache:render (template, view)
end

local function openstream (wreck, taskio, taskid, stream, template)
    local path = render (template, wreck, taskid)
    if not path then
        wreck:die ("output: Failed to render template '"..template.."'")
        return
    end
    -- Make any kvs:// output relative to the current kvsdir
    local key = path:match ("kvs://(.*)$")
    if key then
        path = "kvs://"..tostring (wreck.kvsdir) .."."..key
    end
    taskio:redirect (taskid, stream, path)
    return path
end

local function log (fmt, ...)
    wreck:log_msg (fmt, ...)
end

local function log_err (fmt, ...)
    wreck:log_error (fmt, ...)
end

-- Read `ioservice` entry for this job, and map any stream  with
--  rank == FLUX_NODEID_RANK to this rank.
--
local function fetch_ioservices (wreck)
    local ioservice,err = wreck.kvsdir.ioservice
    if not ioservice then return nil end
    local rank = wreck.flux.rank

    -- Only streams with rank == -1 are handled by
    -- this plugin. Delete other entries and remap FLUX_NODEID_ANY
    -- to this rank:
    for s,v in pairs (ioservice) do
        if v and v.rank == -1 then
            ioservice[s].rank = rank
        else
            ioservice[s] = nil
        end
    end
    return ioservice
end

function rexecd_init ()
    if wreck.nodeid ~= 0 then return end

    local output = kvs_output_config (wreck)
    if not output or not output.files then return end

    local template = output.files.stdout
    local stderr_template = output.files.stderr
    if not template and not stderr_template then return end

    local ntasks = wreck.kvsdir.ntasks
    local ioservices = fetch_ioservices (wreck)

    taskio, err = ioplex.create {
        flux = wreck.flux,
        jobid = wreck.id,
        labelio = output.labelio and output.labelio ~= false,
        log_err = log_err,
        nokz = wreck.kvsdir.options.nokz,
        ioservices = ioservices
    }
    if not taskio then
        wreck:log_error ("Error: %s", err)
        return
    end

    ioplex:enable_debug (log)

    for i = 0, ntasks - 1 do
       if template then
          openstream (wreck, taskio, i, "stdout", template)
       end
       if stderr_template then
           openstream (wreck, taskio, i, "stderr", stderr_template)
       elseif template then
           taskio:dup (i, "stderr", "stdout")
       end
    end
    taskio:start ()
end

function rexecd_exit ()
    if wreck.nodeid ~= 0 or not taskio then return end
    while not taskio:complete() do
        local rc, err = wreck.flux:reactor ("once")
        if not rc then
            log_err ("rexecd_exit: reactor once failed: %s", err or "No error")
            return
        end
    end
    wreck:log_msg ("File io complete")
end

-- vi: ts=4 sw=4 expandtab
