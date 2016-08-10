-- Set environment specific to openmpi
--
-- If configured for SLURM, OpenMPI links with libpmi.so or libpmi2.so
-- (depending on options used).  Flux provides alternate versions of these
-- libraries in a non-default location, and we must set LD_LIBRARY_PATH to
-- point there.
--
-- Some versions of OpenMPI, 1.10.2 for example, set an rpath for their
-- PMI plugins that includes /usr/lib64.  That is bug and those versions
-- need to be patched patched to work with Flux.
-- 
-- If Flux supports the PMI-2 wire protocol, the SLURM libpmi.so might
-- work with Flux.  It currently doesn't, and in fact ignores the protocol
-- version handshake entirely.   (See flux-framework/flux-core#746)


local dirname = require 'flux.posix'.dirname

function rexecd_init ()
    local env = wreck.environ
    local f = wreck.flux
    local libpmi = f:getattr ('conf.pmi_library_path')
    local ldpath = dirname (libpmi)

    if (env['LD_LIBRARY_PATH'] ~= nil) then
        ldpath = ldpath..':'..env['LD_LIBRARY_PATH']
    end
    env['LD_LIBRARY_PATH'] = ldpath
end

-- vi: ts=4 sw=4 expandtab
