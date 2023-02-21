#!/usr/bin/lua
-- Directives embedded in Lua script:
--
-- flux: -N1 --exclusive
--
-- flux: --output=job.out # Set output file
--
local app = require 'app'
app.run()
