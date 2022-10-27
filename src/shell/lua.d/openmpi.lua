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

shell.setenv ("OMPI_MCA_pmix", "flux")
shell.setenv ("OMPI_MCA_schizo", "flux")

-- OpenMPI needs a job-unique directory for vader shmem paths, otherwise
-- multiple jobs per node may conflict (see flux-framework/flux-core#3649).

-- Note: this plugin changes the path for files that openmpi usually shares
-- using mmap (MAP_SHARED) from /dev/shm (tmpfs) to /tmp (maybe tmpfs).
-- Performance may be affected if /tmp is provided by a disk-backed file system.

plugin.register {
    name = "openmpi",
    handlers = {
        {
            topic = "shell.init",
            fn = function ()
                local tmpdir = shell.getenv ("FLUX_JOB_TMPDIR")
                if tmpdir then
                    shell.setenv ("OMPI_MCA_btl_vader_backing_directory", tmpdir)
                end
            end
        }
    }
}

-- vi:ts=4 sw=4 expandtab
