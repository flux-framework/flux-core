-------------------------------------------------------------
-- Copyright 2021 Lawrence Livermore National Security, LLC
-- (c.f. AUTHORS, NOTICE.LLNS, COPYING)
--
-- This file is part of the Flux resource manager framework.
-- For details, see https://github.com/flux-framework.
--
-- SPDX-License-Identifier: LGPL-3.0
-------------------------------------------------------------

if shell == nil then
    error ("This module can only be imported from the Flux job shell")
end

--  Useful functions shared by all subsequently loaded
--   shell Lua plugins

--- Get a top level option from the shell.options table which includes
---  an optional "@version" specification.
--
--  @param name The name of the option to get
--
--  Returns val, version
--   `val` will be nil if shell option was not set
--   `version` will be nil if no version specified.
--
function shell.getopt_with_version (name)
    if not name then return nil end
    local opt = shell.options[name]
    if opt then
        return opt:match("^[^@]+"), opt:match("@(.+)$")
    end
    return nil
end

--- Prepend `path` to colon-separated path environment variable `env_var`
--
--  @param env_var The environment variable to which to prepend
--  @param path    The path to prepend
--
function shell.prepend_path (env_var, path)
    local val = shell.getenv (env_var)

    -- If path is already in env_var, do nothing. We stick ":" on both
    -- ends of the existing value so we can easily match exact paths
    -- instead of possibly matching substrings of paths when trying
    -- to match "zero or more" colons.
    --
    if val and ((":"..val..":"):match (":"..path..":")) then return end

    if val == nil then
       suffix = ''
    else
       suffix = ':'..val
    end
    shell.setenv (env_var, path..suffix)
end

--- Strip all environment variables from job environment that match one
--   or more pattern arguments.
--
function shell.env_strip (...)
    local env = shell.getenv ()
    for k,v in pairs (env) do
        for _,pattern in pairs ({...}) do
            if k:match(pattern) then
                shell.unsetenv (k)
            end
        end
    end
end

--- Set rcpath to the path to the loaded initrc.lua (shell.rcpath) with
--   "/lua.d" appended, plus `FLUX_SHELL_RC_PATH` if set in environment
--   of the  job.
--
local rcpath = shell.rcpath .. "/lua.d"
               .. ":"
               .. (shell.getenv ("FLUX_SHELL_RC_PATH") or "")

--- Source all files matching pattern from rcpath
--
function shell.source_rcpath (pattern)
    for path in rcpath:gmatch ("[^:]+") do
        source (path .. "/" .. pattern)
    end
end

--- Source all files matching value[@version].lua from rcpath
--   for option `opt`
--
function shell.source_rcpath_option (opt)
    local name, version = shell.getopt_with_version (opt)
    if name and name ~= "none" then
        for path in rcpath:gmatch ("[^:]+") do
            local basename = path .. "/" .. opt .. "/" .. name
            source_if_exists (basename .. ".lua")
            if version then
                source_if_exists (basename .. "@" .. version .. ".lua")
            end
        end
    end
end
-- vi: ts=4 sw=4 expandtab
-- 
