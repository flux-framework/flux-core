require 'fluxometer' -- pull in Test.More

local saved = shell.verbose
shell.verbose = false
ok (not shell.verbose,
    "shell.verbose works and returns last value (false)")
shell.verbose = true
ok (shell.verbose,
    "shell.verbose works and returns last value (true)")
shell.verbose = 1
ok (shell.verbose,
    "shell.verbose works and returns last value (1)")
shell.verbose = 0
ok (not shell.verbose,
    "shell.verbose works and returns last value (0)")

shell.verbose = saved

shell.mydata = { foo = "bar" }
type_ok (shell.mydata, "table",
         "can assign to global shell table")

info = shell.info
type_ok (info, "table",
    "shell.info returns table")
type_ok (info.jobid, "number",
    "shell.info.jobid is a number")
type_ok (info.rank, "number",
    "shell.info.rank is a number")
type_ok (info.jobspec, "table",
     "info.jobspec is a table")
is (info.rank, 0,
    "shell.info.rank is expected value");
ok (info.options.standalone,
    "info.options.standalone is true")

jobspec = shell.info.jobspec
type_ok (jobspec, "table",
    "shell.info.jobspec is a table")

rankinfo = shell.rankinfo
type_ok (rankinfo, "table",
    "shell.rankinfo is a table")
is (rankinfo.broker_rank, 0,
    "shell.rankinfo.broker_rank is expected value");
type_ok (rankinfo.ntasks, "number",
    "rankinfo.ntasks is a number")
cmp_ok (rankinfo.ntasks, ">", 0,
    "ntasks is greater than zero")
type_ok (rankinfo.resources, "table",
    "rankinfo.resources is a table")
is (rankinfo.resources.cores, "0,1",
    "rankinfo.resources.cores is expected value")

error_like ("i = task.info", "access task",
    "trying to access task causes error")

rankinfo2 = shell.get_rankinfo (0)
is_deeply (rankinfo, rankinfo2,
    "rankinfo and get_rankinfo match")

plugin.register { name = "done",
    handlers = {
        { topic = "shell.exit", fn = function () done_testing() end }
    }
}

plugin.register {
    name = "test",
    handlers = {
        { topic = "task.*",
          fn = function (topic)
              info = task.info
              type_ok (info, "table",
                  topic .. ": type of task.info is table")
              is (info.localid, 0,
                  topic .. ": task.info.localid is expected value")
              is (info.rank, 0,
                  topic .. ": task.info.rank is expected value")
              type_ok (info.state, "string",
                  topic .. ": task.info.state is string")
              if info.state == "Running" then
                  type_ok (info.pid, "number",
                       "task is running with pid "..info.pid)
              elseif info.state == "Exited" then
                  is (info.wait_status, 0,
                        "task is exited with status 0")
                  is (info.signaled, 0,
                        "task was not signaled")
                  is (info.exitcode, 0,
                        "with exitcode 0")
              end

              -- done_testing() required in task.exec since we're in new proc
              if topic == "task.exec" then done_testing() end
          end
        }
    }
}

-- vi: ts=4 sw=4 expandtab
