--[[--------------------------------------------------------------------------
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
-- Subcommand helper class
--

local function eprintf (...)
    io.stderr:write (string.format (...))
end

local function die (self, fmt, ...)
    -- Add a newline if there is not already one
    if fmt:sub (-1) ~= "\n" then
        fmt = fmt .. "\n"
    end
    eprintf (self.prog..": "..fmt, ...)
    os.exit (1)
end

local function log (self, fmt, ...)
    io.stdout:write (string.format (self.shortprog..": "..fmt, ...))
end

--- Command class from which all "commands" inherit
local Command = { die = die, log = log }
Command.__index = Command

function Command:opt_table ()
    local long_opts = { help = 'h' }
    local short_opts = "h"
    if not self.options then
        return short_opts, long_opts
    end
    for _, t in pairs (self.options) do
        short_opts = short_opts .. t.char .. (t.arg and ":" or "")
        long_opts [t.name] = t.char
    end
    return short_opts, long_opts
end

function Command:fullname ()
    if self.parent then
        return self.parent:fullname () .. " " .. self.name
    end
    return self.prog
end

--
-- Split line in `s` at `columns` colums assuming left pad `pad`
--
local function linesplit (s, columns, pad)
    local width = columns - pad
    local t = { "" }
    if not pad then pad = 0 end
    for prefix,word in s:gmatch("([ \t]*)(%S+)") do
        local total = #(t[#t])
        local len = #word
        if (total + len) > width then
            -- Add new line, pad to pad if pad is given:
            table.insert (t, string.rep (" ", pad))
        end
        -- append word to current line
        t[#t] = t[#t] .. prefix .. word
    end
    return table.concat (t, "\n")
end

function Command:help (code)
    local _, opt_table = self:opt_table ()
    local name = self:fullname ()
    eprintf ("Usage: %s %s\n", name, tostring (self.usage))
    if self.description then
        eprintf ("%s\n", self.description)
    end
    eprintf ("Options:\n")
    eprintf ("  -%s, --%-20s %s\n", 'h', 'help', "Display this message.")
    if self.options then
        local columns = os.getenv ("COLUMNS") or 80
        for _,t in pairs (self.options) do
            local optstr = t.name .. (t.arg and "="..t.arg or "")
            eprintf ("  -%s, --%-20s %s\n",
                     t.char, optstr,
                     linesplit (t.usage, columns - 2, 29))
        end
    end
    if self.CommandsList then
        eprintf ("Supported commands:\n")
        for _, cmd in pairs (self.CommandsList) do
            eprintf (" %-12s %s\n", cmd.name, cmd.description)
        end
    end
    if code then os.exit (code) end
end

-- Build args starting at index
-- arg0 is a command name placed in arg[0]:
local function rebuild_args (args, index, arg0)
    local a = {}
    a[0] = arg0
    for i = index, #args do
        table.insert (a, args[i])
    end
    return a
end

function Command:run (args)
    local getopts = require 'flux.alt_getopt' .get_opts
    local opts, optind = getopts (args, self:opt_table())
    if not opts then self:help (1) end
    if opts.h then self:help (0) end

    self.opt = opts
    self.args = rebuild_args (args, optind)

    -- Check for a subcommand
    local cmdname = self.args[1]
    if self.Commands and cmdname then
        local cmd = self:lookup (cmdname)
        if cmd then
            -- Run command with cmdname in arg[0] and all other args
            --  shifted:
            return cmd:run (rebuild_args (self.args, 2, cmdname))
        end
    end
    self.handler (self, self.args)
end

function Command:lookup (name)
    if not self.Commands then return nil end
    return self.Commands[name]
end

-- a table is passed to Command with fields:
-- @param t.name Name of the script method
-- @param t.description Brief description of this method
-- @param t.usage brief usage to be displayed in help
-- @param t.handle function implementing handler with args (self, arg...)
-- @param t.options extra options (besides --help) for this cmd
function Command:SubCommand (t)
    assert (t and t.name and t.handler)
    t.shortprog = t.name
    t.prog = self.prog.." "..t.name
    t.parent = self

    if not self.Commands then self.Commands = {} end
    self.Commands[t.name] = t

    -- Add to ordered list of commands:
    if not self.CommandsList then self.CommandsList = {} end
    table.insert (self.CommandsList, t)
    return setmetatable (t, Command)
end

-- Default "help" subcommand:
local DefaultHelp = {
    name = "help",
    description = "Display this help or help for a command",
    usage = "[COMMAND]",
    handler = function (self, arg)
        if #arg == 0 then
            self.parent:help (0)
        end
        local cmd = self.parent:lookup (arg[1])
        if not cmd then
            self:die ("Unkown command %s", arg[1])
        end
        cmd:help (0)
    end
} 

-- Create a new subcommand handler object
function create (t)
    if not t then t = {} end
    if not t.name then t.name = arg[0] end
    if not t.usage then t.usage = "[OPTIONS]..." end
    t.prog = t.name:match ("([^/]+)$")
    t.shortprog = t.prog:match ("flux%-(.+)$")
    setmetatable (t, Command)
    t:SubCommand (DefaultHelp)
    return t
end

return { create = create }

--  vi: ts=4 sw=4 expandtab
