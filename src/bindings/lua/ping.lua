#!/usr/bin/lua
--[[--------------------------------------------------------------------------
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
 ---------------------------------------------------------------------------]]
 --
 -- ping.lua: Example script implementing cmb 'ping' client using
 --  Lua bindings for Flux.
 --
local sleep = require 'flux-lua.posix'.sleep
local getopt = require 'flux-lua.alt_getopt'.get_opts

local function printf (...)
    io.stdout:write (string.format (...))
end

local function pad_create (n)
    local t = {}
    for i = 1,n do
        table.insert (t, "p")
    end
    return table.concat (t)
end

local opts, optind = getopt (arg, "P:N:", { pad = "P", count = "N" })

if not arg[optind] then error ("ping: plugin name required") end
local tag = string.format ("%s.ping", arg[optind])

local pad    = opts.P and pad_create (opts.P) or "p"
local t      = require 'flux-lua.timer'.new()
local f, err = require 'flux'.new()
if not f then error (err) end

-- Create a new msghandler
local mh, err = f:msghandler {
    pattern = "*.ping",
    msgtypes = { flux.MSGTYPE_RESPONSE, flux.MSGTYPE_REQUEST },

    handler = function (f, zmsg, mh)
        local resp = zmsg.data
        if resp.errnum then
            error (tag ..": "..posix.errno (resp.errnum))
        end

        local seq = resp.seq
        printf ("%s seq=%d pad=%dB time=%0.3fms\n",
                zmsg.tag, seq, #zmsg.data.pad, t:get() * 1000)

        if opts.N and tonumber(seq) >= tonumber(opts.N) then
            -- Remove this msghandler:
            mh:remove()
            return
        end

        -- Send next ping after 1s
        sleep (1)
        t:set()
        f:send (tag, { seq = tostring(seq+1), pad = pad })
    end
}
if not mh then error(err) end

-- Set initial timer and send our first request. Then enter event loop:
t:set()
local rc,err = f:send (tag, {seq = "1", pad = pad})
if not rc then error (err) end

f:reactor()

-- vi: ts=4 sw=4 expandtab
