-------------------------------------------------------------
-- Copyright 2020 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

local f, err = require 'flux'.new ()
if not f then error (err) end

-- Lua implementation of dirname(3) to avoid pulling in posix module
local function dirname (d)
    if not d:match ("/") then return "." end
    return d:match ("^(.*[^/])/.-$")
end

local libpmi = f:getattr ('conf.pmi_library_path')
local ldpath = dirname (libpmi)
local current = shell.getenv ('LD_LIBRARY_PATH')

if current ~= nil then
  ldpath = ldpath .. ':' .. current
end
shell.setenv ("LD_LIBRARY_PATH", ldpath)
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
