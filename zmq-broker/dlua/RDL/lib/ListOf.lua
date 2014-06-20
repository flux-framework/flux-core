
local function ListOf (arg)
    local ids = arg.ids or arg.hostids
    local T = arg.type or arg[1]
    assert (ids, "ListOf: Required id list argument missing (ids or hostids)")

    if ids:match("^[0-9]") then ids = "["..ids.."]" end

    local hl = hostlist.new (ids)

    if type(T) == 'string' then
        -- Lookup type by name in current environment if we were provided
        --  a string:
        local env = getfenv(1)
        T = env [T]
    end
    assert (T, "ListOf: Missing type: ListOf{'type' or type = 'type'}")

    local t = {}

    -- Hmm, unpack() not working for arg.args, so construct it manually here:
    --
    local args = {}
    for k,v in pairs (arg.args) do
        args[k] = v
    end

    for id in hl:next() do
        args.id = tonumber (id)
        table.insert (t, T (args) )
    end

    return unpack(t)
end

return ListOf
