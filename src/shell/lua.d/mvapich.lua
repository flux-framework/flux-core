-------------------------------------------------------------
-- Copyright 2020 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

if shell.options.mpi == "none" then return end

local f, err = require 'flux'.new ()
if not f then error (err) end

-- Lua implementation of dirname(3) to avoid pulling in posix module
local function dirname (d)
    if not d:match ("/") then return "." end
    return d:match ("^(.*[^/])/.-$")
end

local function setenv_prepend (var, val)
    local path = shell.getenv (var)
    -- If path not already set, then set it to val
    if not path then
        shell.setenv (var, val)
    -- O/w, if val is not already set in path, prepend it
    elseif path:match ("^[^:]+") ~= val then
        shell.setenv (var, val .. ':' .. path)
    end
    -- O/w, val already first in path. Do nothing
end

local libpmi = f:getattr ('conf.pmi_library_path')

setenv_prepend ("LD_LIBRARY_PATH", dirname (libpmi))
shell.setenv ("MPIRUN_NTASKS", shell.info.ntasks)
shell.setenv ("MPIRUN_RSH_LAUNCH", 1)

plugin.register {
    name = "mvapich",
    handlers = {
        {
         topic = "task.init",
         fn = function ()
            local rank = task.info.rank
            task.setenv ("MPIRUN_RANK", rank)
         end
        }
    }
}
