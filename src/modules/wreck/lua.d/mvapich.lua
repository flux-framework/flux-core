-- Set environment specific to mvapich


local dirname = require 'flux.posix'.dirname

-- XXX: MVAPICH2 at least requires MPIRUN_RSH_LAUNCH to be set
--  in the environment or PMI doesn't work (for unknown reason)
--

function rexecd_init ()
    local env = wreck.environ
    local f = wreck.flux
    local libpmi = f:getattr ('conf.pmi_library_path')
    local ldpath = dirname (libpmi)

    if (env['LD_LIBRARY_PATH'] ~= nil) then
        ldpath = ldpath..':'..env['LD_LIBRARY_PATH']
    end

    env['LD_LIBRARY_PATH'] = ldpath
    env['MPIRUN_NTASKS'] = wreck.kvsdir.ntasks
    env['MPIRUN_RSH_LAUNCH'] = 1
end


function rexecd_task_init ()
    local env = wreck.environ
    env['MPIRUN_RANK'] = wreck.taskid
end

-- vi: ts=4 sw=4 expandtab
