local dirname = require 'flux.posix'.dirname

local f, err = require 'flux'.new ()
if not f then error (err) end

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
