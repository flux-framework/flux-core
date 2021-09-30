-- Load all *.so plugins from plugin.searchpath
plugin.load { file = "*.so", conf = {} }

-- Source all rc files under shell.rcpath/lua.d/*.lua of shell rcpath:
shell.source_rcpath ("*.lua")

-- If userrc is set in shell options, then load the user supplied initrc here:
if shell.options.userrc then source (shell.options.userrc) end

-- Uncomment to source user initrc.lua if it exists
-- source_if_exists "~/.flux/shell/initrc.lua"

-- vi: ts=4 sw=4 expandtab
