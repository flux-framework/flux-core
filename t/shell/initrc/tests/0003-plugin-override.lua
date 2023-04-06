require 'fluxometer' -- pulls in Test.More

-- ensure done_testing() is run at shell exit
plugin.register { name = "done",
    handlers = {
      { topic = "shell.exit",
        fn = function () done_testing() end
      }
   }
}
pass ("registered done_testing() plugin")

-- Create a plugin that will be overridden
plugin.register { name = "test",
    handlers = {
      { topic = "*",
        fn = function ()
            fail ("this callback should be overridden")
        end
      }
    }
}
pass ("registered test plugin")

plugin.register { name = "test",
    handlers = {
      { topic = "shell.*",
        fn = function (topic)
            like (topic, "^shell",
                  "correct callback called for "..topic)
        end
      },
      { topic = "task.*",
        fn = function (topic)
            like (topic, "^task",
                  "correct callback called for "..topic)
        end
      }
    }
}

pass ("registered replacement test plugin")

-- vi: ts=4 sw=4 expandtab
