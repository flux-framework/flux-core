-------------------------------------------------------------
-- Copyright 2020 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

-- Set environment specific to spectrum_mpi (derived from openmpi)
--
if shell.options.mpi ~= "spectrum" then return end

local posix = require 'posix'

function prepend_path (env_var, path)
    local val = shell.getenv (env_var)

    -- If path is already in env_var, do nothing. We stick ":" on both
    -- ends of the existing value so we can easily match exact paths
    -- instead of possibly matching substrings of paths when trying
    -- to match "zero or more" colons.
    --
    if val and ((":"..val..":"):match (":"..path..":")) then return end

    if val == nil then
       suffix = ''
    else
       suffix = ':'..val
    end
    shell.setenv (env_var, path..suffix)
end

local function strip_env_by_prefix (env, prefix)
    --
    -- Have to call env:get() to translate env object to Lua table
    --  in order to use pairs() to iterate environment keys:
    --
    for k,v in pairs (env) do
        if k:match("^"..prefix) then
            shell.unsetenv (k)
        end
    end
end

local f = require 'flux'.new()
local rundir = f:getattr ('broker.rundir')

local env = shell.getenv()

-- Clear all existing PMIX_ and OMPI_ values before setting our own
strip_env_by_prefix (env, "PMIX_")
strip_env_by_prefix (env, "OMPI_")

-- Avoid shared memory segment name collisions
-- when flux instance runs >1 broker per node.
shell.setenv ('OMPI_MCA_orte_tmpdir_base', rundir)

-- Assumes the installation paths of Spectrum MPI on LLNL's Sierra
shell.setenv ('OMPI_MCA_osc', "pt2pt")
shell.setenv ('OMPI_MCA_pml', "yalla")
shell.setenv ('OMPI_MCA_btl', "self")
shell.setenv ('OMPI_MCA_coll_hcoll_enable', '0')

-- Help find libcollectives.so
prepend_path ('LD_LIBRARY_PATH', '/opt/ibm/spectrum_mpi/lib/pami_port')
prepend_path ('LD_PRELOAD', '/opt/ibm/spectrum_mpi/lib/libpami_cudahook.so')

plugin.register {
    name = "spectrum",
    handlers = {
        {
         topic = "task.init",
         fn = function ()
            local setrlimit = require 'posix'.setrlimit
            -- Approximately `ulimit -Ss 10240`
            -- Used to silence IBM MCM warnings
            setrlimit ("stack", 10485760)
            local rank = task.info.rank
            task.setenv ("OMPI_COMM_WORLD_RANK", rank)
            end
        }
    }
}


-- vi: ts=4 sw=4 expandtab

