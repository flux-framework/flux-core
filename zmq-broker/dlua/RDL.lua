--
--- RDL.lua : RDL parser driver file.
--
-- First, load some useful modules:
--
local Resource = require 'RDL.Resource'
local hostlist = require 'hostlist'

--
-- Get the base path for this module to use in searching for
--  types in the types db
--  (XXX: This is really a hack temporarily used for this prototype,
--    in the future types will be loaded from system defined path)
local basepath = debug.getinfo(1,"S").source:match[[^@?(.*[\/])[^\/]-$]]

-- Return an table suitable for use as RDL parse environment
local function rdl_parse_environment ()
     --
     -- Restrict access to functions for RDL, which might be user
     --  supplied:
     --
     local env = {
        print = print,
        pairs = pairs,
        tonumber = tonumber,
        assert = assert,
        table = table,
        Resource = Resource,
        hostlist = hostlist
    }
    local rdl = require 'RDL.memstore'.new()
    if not rdl then
        return nil, "failed to load rdl memory store"
    end

    ---
    -- the "uses" function is like 'require' but loads a resource definition
    --  file from the current resource types path
    --
    function env.uses (t)
        local filename = basepath .. "RDL/types/" .. t .. ".lua"
        if env[t] then return end
        local f = assert (loadfile (filename))

        --
        --  Extend access for system defined types. These modules
        --   can access all globals (but only write to the local
        --   environment)
        --
        setfenv (f, setmetatable ({}, {
            __index = function (t,k) return env[k] or _G[k] end
        }))
        local rc, r = pcall (f)
        if not rc then
            return nil, r
        end
        env [t] = r
    end

    ---
    -- Hierarchy() definition function inserts a new hierarchy into
    --  the current RDL db.
    --
    function env.Hierarchy (name)
        return function (resource)
            if not resource.type then
                resource = resource[1]
            end
            assert (resource, "Resource argument required to Hierarchy keyword")
            rdl:hierarchy_put (name, resource)
        end
    end

    ---
    -- Load extra functions defined in RDL/lib/?.lua
    ---
    local glob = require 'posix'.glob
    for _,path in pairs (glob (basepath .. "RDL/lib/*.lua")) do
        local f = assert (loadfile (path))
        local name = path:match("([^/]+)%.lua")
        local rc, r = pcall (f)
        if not rc then
            error ("Failed to load "..path..": "..r)
        end
        env[name] = r
    end

    env.rdl = rdl
    return env
end

--
-- Evaluate RDL in compiled lua function [f] and return RDL representation
--
-- If RDL is in 'config language' format, then the function
--  environment will contain an 'rdl' memstore db object ready
--  to return.
--
-- If RDL is serialized, then running the serialized code will
--  *return* a table in RDL db format, which then must be
--  "blessed" into a memstore object.
--
local function rdl_eval (f)
    local env = rdl_parse_environment ()

    -- Set environment for rdl chunk `f'
    setfenv (f, env)

    local rc, ret = pcall (f)
    if not rc then
        return nil, "Error! " .. ret
    end

    if type (ret) == 'table'  then
        local memstore = require 'RDL.memstore'
        return memstore.bless (ret)
    end

    return env.rdl
end

--
-- Evaluate rdl in string `s'
--
local function rdl_evals (s)
    if type (s) ~= "string" then
        return nil, "refusing to evaluate non-string argument"
    end

    if string.byte (s, 1) == 27 then
        return nil, "binary code prohibited"
    end

    local f, err = loadstring (s)
    if not f then return nil, err end

    return rdl_eval (f)
end

--
-- Load RDL from filename `f'
--
local function rdl_evalf (filename)
    if filename then
        local f, err = io.open (filename, "r")
        if not f then return nil, err end

        local line = f:read()
        f:close()

        if (line:byte (1) == 27) then
            return nil, "binary code prohibited"
        end
    end

    local fn, err = loadfile (filename)
    if not fn then return nil, "RDL eval failed: "..err end

    return rdl_eval (fn)
end


return { eval = rdl_evals, evalf  = rdl_evalf }

-- vi: ts=4 sw=4 expandtab
