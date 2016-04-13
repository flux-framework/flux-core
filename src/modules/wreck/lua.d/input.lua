--
--  Process any stdin configuration for a job, opening files
--   if requested, and setting up task stdin links as
--   directed in lwj.<id>.input.config
--
local posix = require 'flux.posix'

-- temporary "set" class until we have nodeset lua bindings
local taskset = {}
function taskset:__index (key)
    if tonumber (key) then
        return self.t [tostring(key)]
    end
    return rawget (taskset,key)
end
function taskset.new (arg)
    local hostlist = require 'flux.hostlist'
    local t = {}
    local hl = arg and hostlist.new ("["..arg.."]") or hostlist.new ()
    for i in hl:next () do
        t[tostring(i)] = true
    end
    return setmetatable ({ t = t }, taskset)
end
function taskset:len ()
    local count = 0
    for i,v in pairs (self.t) do
        count = count + 1
    end
    return count
end

function taskset:set (i)
    local k = tostring (i)
    local prev = self.t[k]
    self.t[k] = true
    return prev or false
end

function taskset:clear (i)
    local k = tostring (i)
    local prev = self.t[k]
    self.t[k] = nil
    return prev or false
end

function taskset:iterator ()
    return pairs (self.t)
end

----

local function kvs_input_config (wreck)
    local o = wreck.kvsdir ["input.config"]
    if not o then
         o = wreck.flux:kvsdir ("lwj.input.config")
    end
    return o
end

local inputconf = {}
inputconf.__index = inputconf
function inputconf.create (wreck)
    local c = {
        conf = kvs_input_config (wreck),
        wreck = wreck,
        maxid = wreck.kvsdir.ntasks - 1,
        basedir = tostring (wreck.kvsdir),
        inputsdir = tostring (wreck.kvsdir) .. ".input.files",
	inputs = {},
    }
    -- Add default input config
    if not c.conf then
        c.conf = { { src = "stdin", dst = "*" } }
    end

    c.tasksleft, err = taskset.new ('0-'..c.maxid)

    -- Add stdin entry to inputs
    c.inputs[1] = { filename = "stdin",
                    kzpath = c.inputsdir .. ".stdin" }
    return setmetatable (c, inputconf)
end

function inputconf:set (taskid)
    if self.tasksleft[taskid] then
        self.tasksleft:clear(taskid)
        return false
    end
    return true
end

function inputconf:add_source (path)
    local i = { filename = path }
    local f = self.wreck.flux

    -- First open file for input:
    local fp, err = io.open (path, "r")
    if not fp then return nil, err end
    i.fd = posix.fileno (fp)
    
    -- now open kz stream for writing:
    local basedir = self.inputsdir .. "." .. #self.inputs
    f:kvs_put (basedir..".filename", i.filename)
    i.kzpath = basedir .. ".kz"
    local kz, err = f:kz_open (i.kzpath, "w")
    if not kz then
        io.close (fp)
        return nil, err
    end
    i.kz = kz
    table.insert (self.inputs, i)
    return i
end

function inputconf:input (path)
    for _,t in pairs (self.inputs) do
        if t.filename == path then
            return t
        end
    end
    return self:add_source (path)
end

function inputconf:render (template, taskid)
    local lustache = require 'flux.lustache'
    local view = {
        taskid = taskid
    }
    setmetatable (view, { __index = self.wreck })
    return lustache:render (template, view)
end


function inputconf:task_input (template, i)
    return self:input (self:render (template, i))
end

function inputconf:dst_to_list (dst)
    if dst == "" or dst == "*" or dst == "all" then
        return self.tasksleft
    end
    return taskset.new (dst)
end

function inputconf:task_stdin (taskid)
    return self.basedir .. "." .. taskid .. ".stdin"
end

-- Link stdin for task id list ids to "path"
function inputconf:link (ids, path)
    local f = self.wreck.flux
    local taskids = self:dst_to_list (ids)
    if taskids:len() == 0 then return true end
    for i in taskids:iterator () do
        if not self:set (i) then
            local input, err = self:task_input (path, i)
            if not input then return nil, err end
            f:kvs_symlink (self:task_stdin (i), input.kzpath)
        end
    end
    return true
end

function inputconf:process_config ()
    for _, input in pairs (self.conf) do
        local rc, err = self:link (input.dst, input.src)
        if not rc then return nil, err end
    end
    return true
end
function input_start (f, input)
    f:iowatcher {
        fd = input.fd,
        handler = function (iow, r)
            input.kz:write (r.data)
            if r.eof then input.kz:close () end
        end
    }
end

function inputconf:start ()
    local f = self.wreck.flux
    for path, input in pairs (self.inputs) do
        if input.kz and input.fd then
            input_start (f, input)
       end
    end
end
function rexecd_init ()
    if wreck.nodeid ~= 0 then return end
    local f = wreck.flux
    local dir = tostring (wreck.kvsdir)

    local cfg = inputconf.create (wreck)
    local rc, err = cfg:process_config ()
    if not rc then
        wreck:die ("Error: input: %s", err)
        return
    end
    cfg:start ()
end
