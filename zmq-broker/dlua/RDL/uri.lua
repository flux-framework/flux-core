
local URI = {}

local function uri_create (s)
    assert (s, "Failed to pass string to uri_create")
    local uri = {}
    uri.name = s:match ("(%S+):")
    if not uri.name then
        uri.name = s
    else
        uri.path = s:match (":(%S+)$")
    end
    return setmetatable (uri, URI)
end

function URI.__index (uri, key)
    if key == "parent" then
        local path =  uri.path:match ("(%S+)/[^/]+") or "/"
        return uri_create (uri.name..":"..path)
    elseif key == "basename" then
        local b = uri.path:match ("/([^/]+)$")
        return b
    end
    return nil
end

function URI:__tostring ()
    return self.name..":"..(self.path or "/")
end

return { new = uri_create }

-- vi: ts=4 sw=4 expandtab
