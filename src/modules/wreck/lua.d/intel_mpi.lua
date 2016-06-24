-- Set environment specific to Intel MPI


function rexecd_init ()
    local env = wreck.environ
    local f = wreck.flux
    local libpmi = f:getattr ('conf.pmi_library_path')
    env['I_MPI_PMI_LIBRARY'] = libpmi

    -- XXX: Unfortunately, Intel MPI doesn't seem to want to correctly
    --  bootsrap using Flux PMI library unless SLURM_NPROCS and
    --  SLURM_PROCID are correctly set. When Flux supports PMI-1 wire
    --  protocol via PMI_FD, these lines should be able to go away.
    env['SLURM_NPROCS'] = wreck.kvsdir.ntasks
end

function rexecd_task_init ()
    --
    -- See note above about requirement for these SLURM_* environment
    --  variables for Intel MPI PMI-bootstrap functionality
    --
    wreck.environ ['SLURM_PROCID'] = wreck.globalid
end


-- vi: ts=4 sw=4 expandtab
