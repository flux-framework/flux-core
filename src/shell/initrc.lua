-- Skip initrc if running in standalone mode
if shell.info.options.standalone then return end

-- Load all *.so plugins from plugin.searchpath
plugin.load { file = "*.so", conf = {} }

-- Copy jobspec attributes.system.shell.options to shell.options
jobspec = shell.info.jobspec
if jobspec.attributes.system.shell then
    shell.options = jobspec.attributes.system.shell.options or {}
end

-- Source all rc files under shell.rcpath/lua.d/*.lua of shell rcpath:
source (shell.rcpath .. "/lua.d/*.lua")

-- Uncomment to source user initrc.lua if it exists
-- source_if_exists "~/.flux/shell/initrc.lua"

-- vi: ts=4 sw=4 expandtab
