#!/usr/bin/lua

local test = require 'fluxometer'.init (...)
local posix = require_ok ('posix')
local alarm = require_ok ('lalarm')
type_ok (alarm, 'function', 'lalarm: is function')

local success = false
local rc, err = alarm (1, function () diag('in handler'); success = true end)

posix.sleep (2)

ok (success, "lalarm: alarm was triggered")

done_testing()

