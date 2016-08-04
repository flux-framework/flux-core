-- Set environment specific to Intel MPI
--
-- Intel PMI is an MPICH derivative that bootstraps with the PMI-1 wire
-- protocol, or if I_MPI_PMI_LIBRARY is set, a PMI library.
-- 
-- If the library is set, override it so it points to Flux's.
-- If the library is unset, do nothing.
--
-- (N.B. We could just unconditionally unset it, but that would prevent the
-- user from setting it in order to enable client side PMI tracing in the
-- Flux PMI library, enabled by setting FLUX_PMI_DEBUG=1)


function rexecd_init ()
    local env = wreck.environ
    local f = wreck.flux
    local libpmi = f:getattr ('conf.pmi_library_path')

    if env['I_MPI_PMI_LIBRARY'] ~= nil then
        env['I_MPI_PMI_LIBRARY'] = libpmi
    end
end

-- vi: ts=4 sw=4 expandtab
