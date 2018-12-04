--[[--------------------------------------------------------------------------
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
--
-- Task I/O library for WRECK
--


--
--  ostream - simple output stream with reference count.
-- 
local ostream = {}
ostream.__index = ostream
function ostream.create (filename)
    local s = { count = 0, filename = filename, flag = "w" }
    setmetatable (s, ostream)
    return s
end

function ostream:closed ()
    return self.count == 0
end

function ostream:open ()
    if not self.fp then
        if self.filename == "stdout" or self.filename == "stderr" then
            self.fp = io[self.filename]
        else
            self.fp, err = io.open (self.filename, self.flag)
            self.fp:setvbuf 'line'
        end
        if not self.fp then return nil, self.filename..": "..err end
    end
    self.count = self.count + 1
    return true
end

function ostream:close ()
    self.count = self.count - 1
    if self.count == 0 then
        self.fp:close ()
        self.fp = nil
    end
end

function ostream:write (data)
    if not self.fp then return nil, "File not open" end
    local rc, err = self.fp:write (data)
    if not rc then print (err) end
end


--
--  wreck job io tracking object.
--  Direct all task io to a named set of ostream objects
--
local ioplex = {}
ioplex.__index = ioplex


--- Create a wreck i/o multiplexer
-- ioplex objects are an abstraction of wreck reactor-driven I/O
--  with many-to-one or many-to-many output redirection. Named
--  output stream objects are registered with an ioplex object,
--  and then task "stdout" and "stderr" streams are redirected
--  onto these output streams. Output streams have reference counting
--  and are only closed when all references have been closed.
--  When all outputs are closed ioplex:complete() will return true.
--@param arg a Table of named keys for arguments
--@return ioplex object
--
function ioplex.create (arg)
    if not arg.flux or not arg.jobid then
         return nil, "Required handle or jobid arg missing!"
    end

    local io = { 
        flux          = arg.flux,
        id            = arg.jobid,
        labelio       = arg.labelio,
        kvspath       = arg.kvspath,
        on_completion = arg.on_completion,
        log_err       = arg.log_err,
        nofollow      = arg.nofollow,
        removed = {},
        output = {},
        files = {}
    }
    if not io.kvspath then
        local wreck = require 'wreck'
        local r, err = wreck.id_to_path { flux = io.flux, jobid = io.id }
        if not r then error (err) end
        io.kvspath = r
    end
    setmetatable (io, ioplex)
    return io
end

local function eprintf (...)
    io.stderr:write (string.format (...).."\n")
end

function ioplex:enable_debug (logger)
    self.logger = logger and logger or eprintf
end

function ioplex:log (fmt, ...)
    if self.logger then
        self.logger (fmt, ...)
    end
end

function ioplex:err (fmt, ...)
    if self.log_err then
        self.log_err (fmt, ...)
    else
        io.stderr:write (string.format (fmt, ...))
    end
end


local function ioplex_set_stream (self, taskid, name, f)
    if not self.output[taskid] then
        self.output[taskid] ={}
    end
    self.output[taskid][name] = f
end

local function ioplex_create_stream (self, path)
    local files = self.files
    if not files[path] then
        self:log ("creating path %s", path)
        local f, err = ostream.create (path)
        if not f then return nil, err end
        files[path] = f
    end
    return files[path]
end

function ioplex:close_output (of)
    of:close ()
    if not of.fp then
        self:log ("closed path %s", of.filename)
    end
    if self:complete() and self.on_completion then
        self.on_completion()
    end
end

function ioplex:output_handler (arg)
    local taskid = arg.taskid or 0
    local stream = arg.stream or "stdout"
    local data   = arg.data
    local of     = arg.output or self.output[taskid][stream]

    if not data then
        self:close_output (of)
        return
    end
    if self.labelio then
        of:write (taskid..": ")
    end
    of:write (data)
end

local function ioplex_taskid_start (self, flux, taskid, stream)
    local of = self.output[taskid][stream]
    if not of then return nil, "No stream "..stream.." for task " .. taskid  end

    local flags = {}
    if self.nofollow then table.insert (flags, "nofollow") end

    local f = flux
    local key = string.format ("%s.%d.%s", self.kvspath, taskid, stream)
    local iow, err = f:iowatcher {
        key = key,
        kz_flags = flags,
        handler =  function (iow, data, err)
            self:output_handler { taskid = taskid,
                                  stream = stream,
                                  output = of,
                                  data   = data }
        end
    }
    if not iow then
        self:log ("ignoring %s: %s", key, err)
        -- remove reference count
        of:close ()
    end
end

--- "start" all ioplex iowatchers on reactor.
-- @param h flux handle of reactor to use (optional)
function ioplex:start (h)
    local flux = h or self.flux
    if not flux then return nil, "Error: no flux handle!" end

    for taskid, t in pairs (self.output) do
        for stream, f in pairs (t) do
            ioplex_taskid_start (self, flux, taskid, stream)
        end
    end
    self.started = true
end

--- redirect a stream from a task to named stream "path".
-- On first reference to `path` (not including special paths "stdout"
--  and "stderr") the referenced file will be opened with `io.open(..., "w")`
--
-- @param taskid task identifier
-- @param stream name  of stream to redirect (e.g. "stdout")
-- @param path   path of file to redirect to (or "stderr" "stdout")
-- @return true on success, nil with error on failure
--
function ioplex:redirect (taskid, stream, path)
    local f, err = ioplex_create_stream (self, path)
    if not f then return nil, err end

    ioplex_set_stream (self, taskid, stream, f)
    return f:open()
end

--- duplicate a stream from a task onto existing stream for this task.
-- Reference count is incremented for `dst`
-- @param taskid task identifier
-- @param src the source stream to duplicate (e.g. "stderr")
-- @param dst the destination stream to copyto (e.g. "stdout")
--
function ioplex:dup (taskid, src, dst)
    local f = self.output[taskid][dst]
    if not f then
        return nil, 'No such output stream: '..dst
    end
    self.output[taskid][src] = f
    return f:open()
end

--- Check for completion of I/O on an ioplex object
-- @return true if IO is complete, false if not
function ioplex:complete ()
    for _,f in pairs (self.files) do
        if not f:closed() then return false end
    end
    return true
end

return ioplex

-- vi: ts=4 sw=4 expandtab
