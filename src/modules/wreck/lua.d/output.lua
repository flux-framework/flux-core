--
--  Optionally write job output to file(s) if so configured in either
--   lwj.output or lwj.<id>.output
--
--  Output filenames are rendered mustache templates
--
--
local ioplex = require 'wreck.io'

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
    taskio:redirect (taskid, stream, path)
    return path
end

local function log (fmt, ...)
    wreck:log_msg (fmt, ...)
end

local function log_err (fmt, ...)
    wreck:log_error (fmt, ...)
end

function rexecd_init ()
    if wreck.nodeid ~= 0 then return end

    local output = kvs_output_config (wreck)
    if not output or not output.files then return end

    local ntasks = wreck.kvsdir.ntasks
    taskio, err = ioplex.create {
        flux = wreck.flux,
        jobid = wreck.id,
        labelio = output.labelio and output.labelio ~= false,
        log_err = log_err
    }
    if not taskio then wreck:log_msg ("Error: %s", err) end

    ioplex:enable_debug (log)

    local template = output.files.stdout
    local stderr_template = output.files.stderr
    if not template and not stderr_template then return end

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
        wreck.flux:reactor ("once")
    end
    wreck:log_msg ("File io complete")
end
