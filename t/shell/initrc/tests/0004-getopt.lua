require 'fluxometer' -- pull in Test.More

plugin.register { name = "done",
    handlers = {
        { topic = "shell.exit", fn = function () done_testing() end }
    }
}

is (shell.options.test, "test",
    "shell option test has expected value")

shell.options.mpi = "bar"
is (shell.options.mpi, "bar",
    "shell.options.mpi is assignable")

ok (not shell.options.nosuchthing,
    "access of unset shell option returns nil")

shell.options.mpi = nil
ok (not shell.options.mpi,
    "shell.options.mpi can be unset")

-- vi: ts=4 sw=4 expandtab
