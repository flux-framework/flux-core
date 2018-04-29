#!/usr/bin/env lua
local usage = [[
Usage: event-trace [OPTIONS] TOPIC EXIT-EVENT COMMAND

Subscribe to events matching TOPIC and run COMMAND once subscribe
is guaranteed to be active on the flux broker. If EXIT-EVENT is
not an empty string, then exit the process once an event exactly
matching EXIT-EVENT is received.

OPTIONS:
  -h, --help         Display this message
  -e, --exec=CODE    Execute Lua CODE block for each matching event,
                      where `topic` is the topic string of the event
                      and `msg` is the event payload. Default CODE
                      is `print (topic)`.
  -t, --timeout=T    Wait only up to T seconds for EXIT-EVENT.

]]

local flux = require 'flux'
local getopt = require 'flux.alt_getopt'.get_opts

-- Process command line arguments:
local opts, optind = getopt (arg, "he:t:",
                                 { help = 'h',
                                   exec = 'e',
                                   timeout = 't'})
if opts.h then print (usage); os.exit(0) end

-- Topic string base `s` and exit event are next 2 arguments
local s = arg[optind]
local exitevent = arg[optind+1]
if not s or not exitevent then print(usage); os.exit(1) end

-- Command to run is the rest of the argument list
local cmd = ""
for i = optind+2, #arg do
    cmd = cmd .. " " .. arg[i]
end
if cmd == "" then print (usage); os.exit(1) end

-- Compile code to run with each matching event:
local code = opts.e or "print (topic)"
local fn = assert (loadstring ("local topic,msg = ...; "..code))

-- Connect to flux, subscribe, and launch command in background
local f,err = flux.new()
f:subscribe (s)

--- XXX: switch to posix.fork so we can capture failure of cmd?
os.execute (cmd .. " &")

-- Add timer if -t, --timeout was supplied
local tw
if opts.t then
    tw, err = f:timer {
        timeout = opts.t * 1000,
        handler = function (f, to)
               io.stderr:write ("Timeout expired!\n")
               os.exit (1)
        end
    }
end

local mh, err = f:msghandler {
    pattern = s..".*",
    msgtypes = { flux.MSGTYPE_EVENT },
    handler = function (f, msg, mh)
	fn (msg.tag, msg.data)
	if exitevent ~= "" and msg.tag == exitevent then
            mh:remove()
            if tw then tw:remove() end
	end
    end
}

f:reactor()
