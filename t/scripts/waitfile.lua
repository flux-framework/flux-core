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

--- Open a file "filename" and use an iowatcher on flux handle "f"
--    to wait for pattern "p", exiting with 0 exit code if found.
local function tail (f, filename, p)
    local fp, err = io.open (filename)
    if not fp then return nil, err end
    printf ("Opened file %s\n", filename)
    return f:iowatcher {
        fd = posix.fileno (fp),
        handler = function (iow, r)
            if r.data and r.data:match (p) then
                os.exit (0)
            end
       end
    }
end

-- Try opening file -- if it doesn't exist, set a timer for 100ms
--  and try again. Cancel the timer when file is opened successfully
--
if not tail (f, file, pattern) then
    f:timer {
        timeout = 100,
        oneshot = false,
        handler = function (f, to)
           if tail (f, file, pattern) then to:remove() end
        end
    }
end

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
f:reactor ()
printf ("Unexpectedly exited reactor\n")
os.exit (1)
