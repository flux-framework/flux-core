local gpubind = wreck:getopt ("gpubind")
if gpubind == "no" or gpubind == "off" then
    return
end

-- Set CUDA_VISIBLE_DEVICES for all tasks on any rank with one or
--  more "gpu" resources

local gpuinfo = {}
function gpuinfo_create (wreck, gpus)
    local g = {}
    -- Use affinity.cpuset as a convenience to parse the GPU list, which
    --  is in nodeset form (e.g. "0-1" or "0,2-5", etc.)
    --
    local gset, err = require 'flux.affinity'.cpuset.new (gpus)
    if not gset then
        wreck:log_error ("Unable to parse GPU list [%s]: %s", gpus, err)
        return nil
    end
    local g = {
        gpuids = gset:expand (),
        ngpus  = gset:count (),
        ntasks = wreck.tasks_per_node [wreck.nodeid]
    }

    -- If per-task binding is requested, ensure ngpus is evenly divisible
    --  into ntasks:
    if gpubind == "per-task" and g.ngpus % g.ntasks == 0 then
        g.ngpus_per_task = g.ngpus/g.ntasks
    end
    return g
end

function rexecd_init ()
    -- NB: Lua arrays are indexed starting at 1, so this rank's index
    --  into R_lite rank array is nodeid + 1:
    --
    local index = wreck.nodeid + 1

    -- Grab local resources structure from kvs for this nodeid:
    --
    local Rlocal = wreck.kvsdir.R_lite[index].children

    -- If a gpu resource list is set for this rank, then expand it and
    --  set CUDA_VISIBLE_DEVICES to the result:
    --
    local gpus = Rlocal.gpu
    if not gpus then return end

    gpuinfo = gpuinfo_create (wreck, gpus)
    -- If ngpus_per_task is not set, then set CUDA_VISIBLE_DEVICES the same
    --  for all tasks:
    if not gpuinfo.ngpus_per_task then
        local ids = table.concat (gpuinfo.gpuids, ",")
        wreck.environ ["CUDA_VISIBLE_DEVICES"] = ids
    end
    -- Always set CUDA_DEVICE_ORDER=PCI_BUS_ID to ensure CUDA ids match
    --  IDs known to flux scheduler.
    wreck.environ ["CUDA_DEVICE_ORDER"] = "PCI_BUS_ID"
end

function rexecd_task_init ()
    -- If ngpus_per_task is set, then select that many GPUs from the gpuids
    --  list assigned to this rank for the current task:
    if not gpuinfo.ngpus_per_task then return end

    local basis = gpuinfo.ngpus_per_task * wreck.taskid
    local t = {}
    for i = 1,gpuinfo.ngpus_per_task do
        table.insert (t, gpuinfo.gpuids [basis + i])
    end
    wreck.environ ["CUDA_VISIBLE_DEVICES"] = table.concat (t, ",")
end

-- vi: ts=4 sw=4 expandtab
