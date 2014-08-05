uses "Socket"

Node = Resource:subclass ('Node')
function Node:initialize (arg)
    local basename = arg.name or arg.basename
    assert (basename, "Required Node arg `name' missing")

    local id = arg.id
    assert (arg.sockets, "Required Node arg `sockets' missing")
    assert (type (arg.sockets) == "table",
            "Node argument sockets must be a table of core ids")

    Resource.initialize (self,
        { "node",
          id = id,
          name = basename,
          properties = arg.properties or {},
        }
    )
    local sockid = 0
    for _,c in pairs (arg.sockets) do
        self:add_child (Socket{ id = sockid, cpus = c,
                                memory = arg.memory_per_socket })
        sockid = sockid + 1
    end
end

return Node

-- vi: ts=4 sw=4 expandtab
