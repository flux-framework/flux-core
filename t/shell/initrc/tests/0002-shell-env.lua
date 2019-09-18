require 'fluxometer' -- pulls in Test.More

ok (shell.setenv ("FOO", "bar"),   "setenv FOO=bar")
ok (shell.getenv ("FOO") == "bar", "getenv gets env var just set")
ok (shell.unsetenv ("FOO"),        "unsetenv works")
ok (shell.getenv ("FOO") == nil,   "getenv doesn't see var just unset")
ok (shell.setenv ("TEST", "xxxyyyzzz"), "setenv TEST")

env = shell.getenv()
type_ok (env, "table",
    "shell.getenv() returns whole env as table");

is (env.TEST, "xxxyyyzzz",
    "env.TEST returns expected value")

plugin.register {
    name = "test",
    handlers = {
        { topic = "task.init",
          fn = function ()
              ok (task.getenv ("TEST") == "xxxyyyzzz",
                  "task.getenv found expected environment set for task")
              ok (task.setenv ("FOO", "bar"),
                  "task.setenv works")
              is (task.getenv ("FOO"), "bar",
                   "task.setenv gets variable just set")
              task.unsetenv ("TEST")
              ok (not task.getenv ("TEST"),
                   "task.getenv() no longer returns variable")
          end
        },
        { topic = "task.exec",
          fn = function ()
            is (task.getenv ("FOO"), "bar",
                "task.getenv in exec context returns expected result")
            -- required since we're in a different process
            done_testing ()
          end
        },
        { topic = "shell.exit",
          fn = function () done_testing () end
        }
    }
}

-- vi: ts=4 sw=4 expandtab
