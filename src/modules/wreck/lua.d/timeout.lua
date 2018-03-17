--
-- Register timer on nodeid 0 if kvs `walltime` is set for this job.
--  Kill job on timeout if reached.
--
local posix = require 'flux.posix'

local function signal_to_number (s)
    if not s then return nil end
    local ret = tonumber (s)
    if ret then return ret end
    return posix [s]
end

local function timeout_signal (f, wreck)
    --  If 'lwj.walltime-signal' set then return this signal number,
    --   otherwise if 'lwj.walltime-signal' set use this number,
    --   otherwise return default signal 'SIGALRM'
    --
    local s = signal_to_number (wreck.kvsdir ['walltime-signal'])
    if s then return s end
    local s = signal_to_number (f:kvsdir() ["lwj.walltime-signal"])
    if s then return s end
    return posix.SIGALRM
end

local function start_timer (f, wreck, id, walltime)
   local to, err = f:timer {
        timeout = walltime*1000,
        oneshot = true,
        handler = function (f, to)
          local signum = timeout_signal (f, wreck)
          wreck:log_error ("Timeout expired! Killing job with signal %d", signum)
          local rc,err = f:sendevent ({signal = signum}, "wreck.%d.kill", id)
          if not rc then
              wreck:log_msg ("Failed to send kill event: %s", err)
          end
        end
    }
end

function rexecd_init ()
    if wreck.nodeid ~= 0 then return end

    local walltime = tonumber (wreck.kvsdir.walltime)
    if not walltime or walltime <= 0 then return end

    local id = wreck.id
    local f = wreck.flux
    -- start timer when state is running
    local k, err = f:kvswatcher {
        key = tostring (wreck.kvsdir) .. ".state",
        handler = function (k, result)
            if result == "running" then
                 wreck:log_msg ("detected job state = running, starting timer for %ds", walltime)
                 k:remove()
                 start_timer (f, wreck, id, walltime)
            end
        end
    }
end

