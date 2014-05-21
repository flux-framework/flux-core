
local function ClusterOf (arg)
    local ids = arg.ids or arg.hostids
    assert (ids, "ClusterOf: Required id list argument missing (ids or hostids)")

    if ids:match("^[0-9]") then ids = "["..ids.."]" end

    local hl = hostlist.new (ids)

    if type(arg.type) == 'string' then
        local env = getfenv(1)
        arg.type = env [arg.type]
    end
    assert (arg.type, "ClusterOf: Required argument 'type' missing")

    local Constructor = arg.type

    local t = {}

    -- Hmm, unpack() not working for arg.args, so construct it manually here:
    --
    local args = {}
    for k,v in pairs (arg.args) do
        args[k] = v
    end

    for id in hl:next() do
        args.id = id
        table.insert (t, Constructor (args) )
    end

    return t
end

return ClusterOf
