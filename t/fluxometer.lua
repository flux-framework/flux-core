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
 --  Convenience module for running Flux lua bindings tests. Use as:
 --
 --  local test = require 'fluxometer'.init (...)
 --  test:start_session { size = 2 }
 --
 --  The second line is optional if a test does not require a flux
 --   session in order to run.
 --
 --  Other convenience methods in the test object include
 --
 --  test:say ()  -- Issue debug output as diagnostics
 --  test:die ()  -- bail out of tests
 --
 ---------------------------------------------------------------------------
---------------------------------------------------------------------------
--
--  Initialize fluxTest object with fluxometer configuration.
--
--  fluxometer.conf initializes package.path and cpath, so this must
--  remain the first line in this file!
--
--  If FLUXOMETER_LUA_PATH is set, place this path at the front of
--  package.path so we load the same fluxometer.conf as before.
--
local unpack = table.unpack or unpack
local fpath = os.getenv ("FLUXOMETER_LUA_PATH")
if fpath then
    package.path = fpath .. ';' .. package.path
end

local fluxTest = require 'fluxometer.conf'
fluxTest.__index = fluxTest

--  Load other requirements:
local getopt = require 'flux.alt_getopt'.get_opts
local posix = require 'flux.posix'

---
--  Append path p to PATH environment variable:
--
local function do_path_prepend (p)
    posix.setenv ("PATH", p .. ":" .. os.getenv ("PATH"))
end

---
--  Reinvoke "current" test script under a flux session using flux-start
--
function fluxTest:start_session (t)
    -- If fluxometer session is already active just return:
    if self.flux_active then return end
    posix.setenv ("FLUXOMETER_ACTIVE", "t")

    local size = t.size or 1
    local extra_args = t.args or {}
    local cmd = { self.flux_path, "start",
                  unpack (self.start_args),
                  string.format ("--test-size=%d", size) }

    if t.args then
        for _,v in pairs (t.args) do
            table.insert (cmd, v)
        end
    end

    table.insert (cmd, self.arg0)

    if (self.opts.r) then
        table.insert(cmd, "--root="..self.opts.r)
    end

    -- Set FLUXOMETER_LUA_PATH to ensure we load the same fluxometer.conf
    --  after `flux start ...`
    posix.setenv ("FLUXOMETER_LUA_PATH", self.src_dir..'/?.lua')

    -- reexec script under flux-start if necessary:
    --  (does not return)
    local r, err = posix.exec (unpack (cmd))
    error (err)
end


function fluxTest:say (...)
    diag (self.prog..": "..string.format (...))
end


function fluxTest:die (...)
    BAIL_OUT (self.prog..": "..string.format (...))
end


---
--   Create fluxometer test object
--
function fluxTest.init (...)
    local debug = require 'debug'
    local test = setmetatable ({}, fluxTest)

    if os.getenv ("FLUXOMETER_ACTIVE") then
        test.flux_active = true
    end

    -- Get path to current test script using debug.getinfo:
    test.arg0 = debug.getinfo (2).source:sub (2)
    test.prog = test.arg0:match ("/*([^/]+)%.t")

    -- Support the same common arguments as sharness:
    test.opts, optind = getopt (arg, "dvr:",
                                 { debug = 'd',
                                   verbose = 'v',
                                   root = 'r'
                                 })

    local cwd, err = posix.getcwd ()
    if not cwd then error (err) end
    test.src_dir = cwd

    -- If arg0 doesn't contain absolute path, then assume
    --  local directory and prepend src_dir
    if not test.arg0:match ('^/') then
        test.arg0 = test.src_dir..'/'..test.arg0
    end

    test.log_file = "lua-"..test.prog..".broker.log"
    test.start_args = { "-Slog-filename=" .. test.log_file }

    local path = fluxTest.fluxbindir .. "/flux"
    local mode = posix.stat (path, 'mode')
    if mode and mode:match('^rwx') then
        do_path_prepend (fluxTest.fluxbindir)
        test.flux_path = path
    else
        test:die ("Failed to find flux path")
    end

    test.trash_dir = "trash-directory.lua-"..test.prog
    if test.opts.r then
        test.trash_dir = test.opts.r .. "/" .. test.trash_dir
    end
    if test.flux_active then
        os.execute ("rm -rf "..test.trash_dir)
        os.execute ("mkdir -p "..test.trash_dir)
        posix.chdir (test.trash_dir)
        cleanup (function ()
                     posix.chdir (test.src_dir)
                     os.execute ("rm "..test.log_file)
                     os.execute ("rm -rf "..test.trash_dir)
                 end)
    end
    return test
end

return fluxTest
-- vi: ts=4 sw=4 expandtab
