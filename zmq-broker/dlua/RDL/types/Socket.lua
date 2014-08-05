Socket = Resource:subclass ('Socket')

function Socket:initialize (arg)
    local cpuset = require 'cpuset'.new

    assert (tonumber(arg.id),   "Required Socket arg `id' missing")
    assert (type(arg.cpus) == "string", "Required Socket arg `cpus' missing")

    Resource.initialize (self,
        { "socket",
          id = arg.id,
          properties = { cpus = arg.cpus }
        }
    )
    --
    -- Add all child cores:
    --
    local id = 0
    local cset = cpuset (arg.cpus)
    for core in cset:setbits() do
        self:add_child (
            Resource{ "core", id = core, properties = { localid = id }}
        )
        id = id + 1
    end

    if arg.memory and tonumber (arg.memory) then
        self:add_child (
            Resource{ "memory", size = arg.memory }
        )
    end
end

return Socket

-- vi: ts=4 sw=4 expandtab
