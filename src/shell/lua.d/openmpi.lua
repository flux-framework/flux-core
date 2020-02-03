-------------------------------------------------------------
-- Copyright 2020 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

local f = require 'flux'.new ()
local rundir  = f:getattr ('broker.rundir')
shell.setenv ("OMPI_MCA_orte_tmpdir_base", rundir)
