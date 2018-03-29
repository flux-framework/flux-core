-- Set pmi.PMI_process_mapping


-- compute blocks list as a Lua table from tasks_per_node and nnodes:
local function compute_blocks (cpn, nnodes)
    local blocks = {}
    local last = nil
    for i = 0, nnodes - 1 do
        local count = cpn [i]
        if last and cpn[i] == last.tasks then
            last.count = last.count + 1
        else
            last = { start = i, count = 1, tasks = cpn [i] }
            table.insert (blocks, last)
        end
    end
    return blocks
end

-- return 'standard' PMI_process_mapping vector as a string
local function blocks_to_pmi_mapping (blocks)
    if not blocks then return " " end
    local s = "(vector"
    for _,b in pairs (blocks) do
        s = s .. string.format (",(%d,%d,%d)", b.start, b.count, b.tasks)
    end
    s = s .. ")"
    return (s)
end

function rexecd_init ()
    if (wreck.nodeid ~= 0) then return end

    local blocks = compute_blocks (wreck.tasks_per_node, wreck.nnodes)
    local mapping = blocks_to_pmi_mapping (blocks)
    wreck.kvsdir ["pmi.PMI_process_mapping"] = mapping
end

