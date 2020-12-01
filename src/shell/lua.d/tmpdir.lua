-------------------------------------------------------------
-- Copyright 2020 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------
--
-- Ensure TMPDIR exists if it is set in job environment
--
local tmpdir = shell.getenv ("TMPDIR")
if tmpdir then
    local rc, exit, signal = os.execute ("umask 077; mkdir -p "..tmpdir)
    if not rc then
        shell.log_error ("Failed to create TMPDIR="..tmpdir)
    end
end


