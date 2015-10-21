#!/usr/bin/lua
--
--  waitfile.lua : Wait until a named file appears and contains a pattern
--
local usage = [[
Usage: waitfile.lua TIMEOUT PATTERN FILE

Wait up to TIMEOUT seconds for FILE to appear and contain at least
one line matching PATTERN. PATTERN is a Lua style pattern match.
]]

local flux = require 'flux'
local posix = require 'flux.posix'

local f, err = flux.new()
if not f then error (err) end

local timeout = arg[1]
local pattern = arg[2]
local file = arg[3]

local function printf (m, ...)
    io.stderr:write (string.format ("waitfile: "..m, ...))
end

if not timeout or not pattern or not file then
    io.stderr:write (usage)
    os.exit (1)
end

local filewatcher = {}
filewatcher.__index = filewatcher

function filewatcher:checklines ()
    local fp, err = io.open (self.filename, "r")
    if not fp then return nil, err end
    local p, err = fp:seek ("set", self.position)
    for line in fp:lines() do
        if self.printlines then io.stdout:write (line.."\n") end
        if line:match (self.pattern) then return true end
    end
    self.position = fp:seek()
    return false
end

-- Make older posix stat table look like new
local stat = {
 __index = function (t, k)
    local k2 = k:match ("st_(.+)")
    local v = rawget (t, k)
    return v and v or rawget (t, k2)
 end
}

function filewatcher:changed ()
    local st = self.st
    local prev = self.prev
    if not st then return false end -- No file yet
    if not prev then return true end
    if st.st_mtime > prev.st_mtime then
        if st.st_size < prev.st_size then
            printf ("truncated!\n")
            self.position = 0 -- reread
        end
        return true
    end
    return false
end

function filewatcher:check ()
    if self:changed () and (self.pattern == "" or self:checklines ()) then
        self:on_match ()
        return true
    end
    self.prev = self.st
end

function filewatcher:start ()
    local st = posix.stat (self.filename)
    if st then
        self.st = setmetatable (st, stat)
    end
    self.flux:statwatcher {
        path = self.filename,
        interval = self.interval,
        handler = function (w, st, prev)
            printf ("wakeup\n")
            self.st = setmetatable (st, stat)
            self:check ()
        end
    }
    self:check ()
end

setmetatable (filewatcher, { __call = function (t, arg)
    if not arg.filename or not arg.pattern then
        return nil, "Error: required argument missing"
    end
    if not arg.flux then
        return nil, "Error: flux handle missing"
    end
    local w = {
        flux =     arg.flux,
        filename = arg.filename,
        pattern  = arg.pattern,
        interval = arg.interval and arg.interval or .25,
        on_match = arg.on_match,
        position = 0,
        printlines = true,
    }
    setmetatable (w, filewatcher)
    if not w.on_match then
        w.on_match = function () os.exit (0) end
    end
    return w
end
})

local fw, err = filewatcher { flux = f, filename = file, pattern = pattern }
if not fw then printf ("%s\n", err); os.exit (1) end

-- Exit with non-zero status after timeout:
--
f:timer {
    timeout = timeout * 1000,
    handler = function ()
        printf ("Timeout after %ds\n", timeout)
        os.exit (1)
    end
}

-- Start reactor to do the work. It is an error if we exit the reactor.
fw:start ()
f:reactor ()
printf ("Unexpectedly exited reactor\n")
os.exit (1)
