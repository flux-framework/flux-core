
--
--  Set WRECK environment from environment table [environ]
--
local function rexec_set_env (environ)
    if not environ then return end

    local env = wreck.environ
    for k,v in pairs (environ) do
        env[k] = v
    end
end
--
-- Set common environment and working directory for all tasks
--
function rexecd_init ()
    local posix = require 'flux.posix'

    -- Initialize environment with top level lwj.environ
    local k = wreck.flux:kvsdir("lwj")
    rexec_set_env (k.environ)

    -- Get kvsdir handle to this LWJ's entry in KVS and override
    --  with lwj.<id>.environ
    --
    local lwj = wreck.kvsdir
    rexec_set_env (lwj.environ)

    -- Check for current working directory as lwj.<id>.cwd
    local cwd = lwj.cwd
    if cwd then
        posix.chdir (cwd)
    end
end

--
-- Per-task version of above:
--  If lwj.<taskid>.environ or cwd exist then these override settings
--  from lwj.{environ,cwd}:
--
function rexecd_task_init ()
    local posix = require 'flux.posix'
    local taskid = wreck.taskid;

    local task = wreck.by_task
    if not task then return end

    --  Set any task-specific environ from lwj.<id>.<taskid>.environ
    rexec_set_env (task.environ)
    
    --  Each task can have a different cwd:
    local cwd = task.cwd
    if cwd then
        posix.chdir (cwd)
    end
end
-- vi: ts=4 sw=4 expandtab
