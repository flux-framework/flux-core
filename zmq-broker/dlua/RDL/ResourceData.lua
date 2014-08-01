local ResourceData = {}
ResourceData.__index = ResourceData

local function deepcopy (t)
    if type(t) ~= 'table' then return t end
    local mt = getmetatable (t)
    local r = {}
    for k,v in pairs (t) do
        r [k] = deepcopy (v)
    end
    return setmetatable (r, mt)
end

---
-- Single level copy of a table from arg[name]
--
local function copy_list_from_args (arg, name)
    local dst = {}
    if arg[name] then
        local src = arg[name]
        if type(src) ~= "table" then
            error (name.." is not a table")
        end
	for k,v in ipairs (src) do
            dst[v] = 1
            src[k] = nil
	end
        for k,v in pairs (src) do
            if type(v) == "table" then
                error ("Augh! Unexpected table!")
            end
            dst[k] = v
        end
    end
    return dst
end

function ResourceData:__tostring()
    local name = rawget (self, "name") or rawget (self, "type")
    local id = rawget (self, "id") or ""
    return string.format ("%s%s", name, id)
end

function ResourceData:create (args)

    -- Type given as { type = <t>, ... } or { <t>, ...}
    local t = args.type or args[1]

    -- Name given as { name = <n>, ... } or { <t>, <n>, ... }
    local name = args.name or args[2] or t

    -- Type is required, if no resource name, then name = type
    if not t then return nil, "Resource type required" end

    local R = {
        type = t,
        uuid = args.uuid or require 'RDL.uuid'(),
        name = name,
        id = args.id or nil,
        properties = copy_list_from_args (args, "properties"),
        tags = copy_list_from_args (args, "tags"),

        -- Initialize total size of this resource and set
        --  currently allocated to 0. The default size for all resource
        --  is 1.
        size = args.size or args.count or 1,
        allocated = 0,
        hierarchy = {},
    }
    setmetatable (R, ResourceData)

    return R
end

local function hierarchy_basename (uri)
    return uri:match ("([^/]+)/?")
end

function ResourceData:add_hierarchy (uri)
    local name = hierarchy_basename (uri)
    if not name then
        return nil, "Unable to determine basename of hierarchy"
    end
    if self.hierarchy [name] then
        return nil, "Hierarchy already exists"
    end
    --
    -- Store this hierarchy's name along with path (uri) to this
    --  resource within that hieararchy:
    --
    self.hierarchy [name] = uri
    return name, uri
end

function ResourceData:remove_hierarchy (uri)
    local name = hieararchy_basename (uri)
    self.hierarchy [name] = nil
    return true
end

setmetatable (ResourceData, { __call = ResourceData.create })

return ResourceData
