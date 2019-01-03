#!/usr/bin/env lua

-------------------------------------------------------------
-- Copyright 2014 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

--
--  waitfile.lua : Wait until a named file appears and contains a pattern
--
local usage = [[
Usage: waitfile.lua [OPTIONS] FILE

Wait for FILE to appear with optional pattern and number of lines.

Options:
 -h, --help       Display this message
 -q, --quiet      Do not print file lines on stdout
 -v, --verbose    Print extra information about program operation
 -p, --pattern=P  Only match lines with Lua pattern P. Default is the
                  empty pattern (therefore anything or nothing matches)
 -c, --count=N    Wait until pattern has matched on N lines (Default: 1)
 -t, --timeout=T  Timeout and exit with nonzero status after T seconds.
                  (Default is to wait forever)
 -P, --plugin=F   Load a plugin from file F or stdin if F is `-'. The plugin
                  can register its own callbacks with flux handle `f'
]]

local getopt = require 'flux.alt_getopt' .get_opts
local posix  = require 'flux.posix'
local flux   = require 'flux'
local timer  = require 'flux.timer'

local opts, optind = getopt (arg, "hvqc:t:p:P:",
                             { timeout = 't',
                               pattern = 'p',
                               plugin = 'P',
                               count = 'c',
                               quiet = 'q',
                               verbose = 'v',
                               help = 'h'
                             })
if opts.h then print (usage); os.exit (0) end

local f, err = flux.new()
if not f then error (err) end

local timeout = tonumber (opts.t or 0)
local pattern = opts.p or ""
local count = opts.c or 1
local file = arg[optind]
local plugin = opts.P

local tt = timer.new()

local function printf (m, ...)
    io.stderr:write (string.format ("waitfile: %s: %4.03fs: "..m, file, tt:get0(), ...))
end

local function log_verbose (m, ...)
    if opts.v then
        printf (m, ...)
    end
end

if not timeout or not pattern or not file then
    io.stderr:write (usage)
    os.exit (1)
end

local filewatcher = {}
filewatcher.__index = filewatcher

function filewatcher:check_line (line)
    if line:match (self.pattern) then
        self.nmatch = self.nmatch + 1
        log_verbose ("Got match (%d/%d)\n", self.nmatch, self.count)
        return self.nmatch == self.count
    end
    return false
end

function filewatcher:checklines ()
    if self.st.st_size == 0 then
        -- Check if empty file is a match
        return self:check_line ("")
    end

    local fp, err = io.open (self.filename, "r")
    if not fp then return nil, err end
    local p, err = fp:seek ("set", self.position)
    log_verbose ("%s seek set to %d\n", self.filename, self.position)
    for line in fp:lines() do
        if self.printlines then io.stdout:write (line.."\n") end
        if self:check_line (line) then
            return true
        end
    end
    self.position = fp:seek()
    log_verbose ("leaving checklines() position=%d\n", self.position)
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
    if not st then
        log_verbose ("No file yet\n")
        return false
    end
    if not prev then
        log_verbose ("File appeared with size %d\n", st.st_size)
        return true
    end
    log_verbose ("File changed size %d\n", st.st_size)
    if st.st_size < prev.st_size then
        printf ("truncated!\n")
        self.position = 0 -- reread
    end
    return true
end

function filewatcher:check ()
    if self:changed () and self:checklines () then
        log_verbose ("Got match. Calling self:on_match()\n")
        self:on_match ()
        return true
    end
    self.prev = self.st
    return false
end

function filewatcher:start ()
    self.flux:statwatcher {
        path = self.filename,
        interval = self.interval,
        handler = function (w, st, prev)
            log_verbose ("wakeup\n")
            self.st = setmetatable (st, stat)
            self:check ()
            log_verbose ("back to sleep\n")
        end
    }
    --  Be sure to call initial stat(2) *after*
    --  statwatcher is started above, to avoid any race
    local st = posix.stat (self.filename)
    if st then
        self.st = setmetatable (st, stat)
    end
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
	    count    = tonumber (arg.count) or 1,
        interval = arg.interval and arg.interval or .25,
        on_match = arg.on_match,
        position = 0,
        nmatch   = 0,
        printlines =  not arg.quiet,
    }
    setmetatable (w, filewatcher)
    if not w.on_match then
        w.on_match = function ()
	    log_verbose ("Exiting\n")
            os.exit (0)
        end
    end
    return w
end
})

local fw, err = filewatcher { flux = f,
                              filename = file,
                              pattern = pattern,
                              count = count,
                              quiet = opts.q }
if not fw then printf ("%s\n", err); os.exit (1) end

-- Exit with non-zero status after timeout:
--
if timeout > 0 then
    f:timer {
      timeout = timeout * 1000,
      handler = function ()
        printf ("Timeout after %ds\n", timeout)
        os.execute ("ls -l "..file)
        os.exit (1)
      end
    }
end


-- open and execute a plugin file provided on cmdline
if plugin then
    local file
    if plugin == "-" then
        file = io.stdin
    else
        local f,err = io.open (plugin, "r")
        if not f then error (err) end
        file = f
    end
    local code = file:read ("*a")
    local fn, err = loadstring ("local f, printf = ...; " .. code);
    if not fn then error (err) end
    local r, v = pcall (fn, f, printf)
    if not r then error (v) end
end

-- Start reactor to do the work. It is an error if we exit the reactor.
fw:start ()
f:reactor ()
printf ("Unexpectedly exited reactor\n")
os.exit (1)
