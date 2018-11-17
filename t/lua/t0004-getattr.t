#!/usr/bin/env lua
--
--  Test getattr
--
local t = require 'fluxometer'.init (...)
t:start_session { size = 1 }

plan (6)

local flux = require_ok ('flux')
local f, err = flux.new()
type_ok (f, 'userdata', "create new flux handle")
is (err, nil, "error is nil")

--
--  Test getattr
--
local attrval = f:getattr "size"
is (attrval, "1", "correctly got broker size attr")

local v,err = f:getattr "nosuchthing"
is (v, nil, "Non-existent attr has error")
is (err, "No such file or directory", "got expected error string")
