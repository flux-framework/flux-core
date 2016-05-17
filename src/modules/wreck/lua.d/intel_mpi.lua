-- Set environment specific to Intel MPI


function rexecd_init ()
    local env = wreck.environ
    local f = wreck.flux
    local libpmi = f:getattr ('conf.pmi_library_path')
    env['I_MPI_PMI_LIBRARY'] = libpmi
end

-- vi: ts=4 sw=4 expandtab
