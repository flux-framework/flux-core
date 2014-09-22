local ResourceData = require "RDL.ResourceData"
local class = require "middleclass"

---
-- A Resource is a hierarchical reference to ResourceData
--
local Resource = class ('Resource')

function Resource:initialize (args)
    self.children = {}
    if args.children then
        for _,r in pairs (args.children) do
            self:add_child (r)
        end
        args.children = nil
    end
    self.resource = ResourceData (args)
end

function Resource:add_child (r)
    table.insert (self.children, r)
    r.parent = self
    return self
end

function Resource:children ()
    return pairs(self.children)
end

function Resource:__tostring ()
    return tostring (self.resource)
end

function Resource:__concat (x)
    return tostring (self) .. tostring (x)
end

return Resource
-- vi: ts=4 sw=4 expandtab
