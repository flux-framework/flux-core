local f = require 'flux'.new ()
local rundir  = f:getattr ('broker.rundir')
shell.setenv ("OMPI_MCA_orte_tmpdir_base", rundir)
