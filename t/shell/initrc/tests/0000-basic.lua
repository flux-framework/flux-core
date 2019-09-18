require 'fluxometer' -- pulls in Test.More

local file = debug.getinfo (1, "S").source:sub(2)

local function dirname (s)
    return s:gsub ('/[^/]*$', '')
end

plugin.register { name = "done",
    handlers = {
        { topic = "shell.exit", fn = function () done_testing() end }
    }
}

error_like ('source "foo"', "source foo: No such file or directory",
    "source of non-pattern filename dies on error")

lives_ok ('source "/nosuchdir/*.lua"',
    "source of pattern filename ignores nomatch by default")

lives_ok ('source_if_exists "foo"',
    "source_if_exists skips missing file")

type_ok (shell.rcpath, "string",
    "shell.rcpath is a string")
is (shell.rcpath, dirname (file),
    "shell.rcpath is the directory to this file")

type_ok (plugin.searchpath, "string",
    "plugin.searchpath returns a string")

local saved = plugin.searchpath
lives_ok ('plugin.searchpath = plugin.searchpath .. ":/tmp/foo"',
    "plugin.searchpath can be assigned")
plugin.searchpath = saved
is (plugin.searchpath, saved,
    "plugin.searchpath restored")

-- vi: ts=4 sw=4 expandtab
