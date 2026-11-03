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

local resource = require 'posix.sys.resource'

-- Clear all existing PMIX_ and OMPI_ values before setting our own
shell.env_strip ("^PMIX_", "^OMPI_")

-- Assumes the installation paths of Spectrum MPI on LLNL's Sierra
shell.setenv ('OMPI_MCA_osc', "pt2pt")
shell.setenv ('OMPI_MCA_pml', "yalla")
shell.setenv ('OMPI_MCA_btl', "self")
shell.setenv ('OMPI_MCA_coll_hcoll_enable', '0')

-- Help find libcollectives.so
shell.prepend_path ('LD_LIBRARY_PATH',
                    '/opt/ibm/spectrum_mpi/lib/pami_port')
shell.prepend_path ('LD_PRELOAD',
                    '/opt/ibm/spectrum_mpi/lib/libpami_cudahook.so')

plugin.register {
    name = "spectrum",
    handlers = {
        {
         topic = "task.init",
         fn = function ()
            -- Approximately `ulimit -Ss 10240`
            -- Used to silence IBM MCM warnings
            local lim = resource.getrlimit (resource.RLIMIT_STACK)
            if lim then
                lim.rlim_cur = 10485760
                resource.setrlimit (resource.RLIMIT_STACK, lim)
            end
            local rank = task.info.rank
            task.setenv ("OMPI_COMM_WORLD_RANK", rank)
            end
        }
    }
}

-- vi: ts=4 sw=4 expandtab

