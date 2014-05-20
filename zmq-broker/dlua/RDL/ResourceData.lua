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

    local properties = {}

    if args.properties then
        if type(args.properties) ~= "table" then
            return nil, "element properties must be a table"
        end
        properties = deepcopy (args.properties)
    end

    local R = {
        type = t,
        uuid = args.uuid or require 'RDL.uuid'(),
        name = name,
        id = args.id or nil,
        properties = properties,
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
