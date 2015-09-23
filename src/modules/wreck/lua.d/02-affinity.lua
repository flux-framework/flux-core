local setaffinity = require 'flux.affinity'.setaffinity

local function do_setaffinity (c)
    if c and tostring (c) then
        local r, err = setaffinity (c)
	if not r then error (err) end
    end
end
--
-- Set cpu affinity if lwj.<id>.<rank>.cpumask is set:
--
function rexecd_init ()
    do_setaffinity (wreck.by_rank.cpumask)
end

--
-- Per-task version of above:
--  If lwj.<id>.<taskid>.cpumask exists then set for this task:
--
function rexecd_task_init ()
    if wreck.by_task then
        do_setaffinity (wreck.by_task.cpumask)
    end
end
-- vi: ts=4 sw=4 expandtab
