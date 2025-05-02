#!/bin/sh

test_description='Test flux modprobe'

. `dirname $0`/sharness.sh

test_under_flux 2

seq=0
test_expect_success 'modprobe can run a task' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
	@task("test")
	def task(context):
	    print("ran a task")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	grep "ran a task" output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe can run multiple tasks' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
	@task("test")
	def task1(context):
	    print("ran a task")
	@task("test2")
	def task2(context):
	    print("ran another task")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	grep "ran a task" output${seq} &&
	grep "ran another task" output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe setup() runs before tasks' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	def setup(context):
	    print("setup")

	@task("task")
	def task(context):
	    print("task")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	setup
	task
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe before=* works' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
	def setup(context):
	    print("setup")

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last")
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	setup
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs prevents task from running' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first")
	def first(context):
	    print("first")

	@task("last", needs=["noexist"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs works' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", needs=["first"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs_attrs prevents task from running' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
@task("first")
	def first(context):
	    print("first")

	@task("last", needs_attrs=["noexist"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs_attrs works' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", needs_attrs=["needed"])
	def last(context):
	    print("last")
	EOF
	flux setattr needed 1 &&
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs_config prevents task from running' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first")
	def first(context):
	    print("first")

	@task("last", needs_config=["testconfig"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs_config works' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", needs_config=["needed"])
	def last(context):
	    print("last")
	EOF
	flux config load <<-EOF &&
	needed = 1
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe ranks prevents task from running' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first")
	def first(context):
	    print("first")

	@task("last", ranks=">0")
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe ranks works' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", ranks=">0")
	def last(context):
	    print("last")
	EOF
	flux exec -r 1 flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe ranks=0 runs task on rank 1' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first")
	def first(context):
	    print("first")

	@task("last", ranks="0", after=["first"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe ranks=0 prevents task from running on rank 1' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", ranks="0")
	def last(context):
	    print("last")
	EOF
	flux exec -r 1 flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs works with ranks' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("needed", ranks="0")
	def needed(context):
	    print("needed")

	@task("last", needs=["needed"], after=["needed"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	needed
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs works with ranks to prevent task' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("needed", ranks="0")
	def needed(context):
	    print("needed")

	@task("last", needs=["needed"], after=["needed"])
	def last(context):
	    print("last")
	EOF
	flux exec -r 1 flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs works recursively' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("needed", ranks="1")
	def needed(context):
	    print("needed")

	@task("next", needs=["needed"], after=["needed"])
	def last(context):
	    print("last")

	@task("last", needs=["next"], after=["next"])
	def last(context):
	    print("last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	EOF
	test_cmp test${seq}.expected output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe needs works recursively (all tasks enabled)' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("needed", ranks="1")
	def needed(context):
	    print("needed")

	@task("next", needs=["needed"], after=["needed"])
	def next(context):
	    print("next")

	@task("last", needs=["next"], after=["next"])
	def last(context):
	    print("last")
	EOF
	flux exec -r 1 flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	first
	needed
	next
	last
	EOF
	test_cmp test${seq}.expected output${seq}
'
test_expect_success 'modprobe: create test modprobe.toml' '
	mkdir etc &&
	cat <<-EOF >etc/modprobe.toml
	[[modules]]
	name = "barrier"
	[[modules]]
	name = "heartbeat"
	[[modules]]
	name = "content"
	requires = ["heartbeat", "content-backing"]
	[[modules]]
	name = "content-sqlite"
	ranks = "0"
	after = ["content"]
	provides = ["content-backing"]
	[[modules]]
	name = "kvs"
	requires = ["content", "heartbeat"]
	after = ["content-backing", "content"]
	[[modules]]
	name = "kvs-watch"
	requires = ["kvs"]
	after = ["kvs"]
	[[modules]]
	name = "resource"
	after = ["kvs-watch"]
	requires = ["kvs-watch"]
	[[modules]]
	name = "job-manager"
	ranks = "0"
	requires = ["resource", "kvs"]
	after = ["resource", "kvs"]
	[[modules]]
	name = "sched-simple"
	ranks = "0"
	provides = ["sched", "feasibility"]
	requires = ["job-manager", "resource"]
	after = ["job-manager", "resource"]
	EOF
'
test_expect_success 'modprobe can list dependencies for modules' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe list-dependencies sched >list-depends.out &&
	cat <<-EOF > list-depends.expected &&
	sched (sched-simple)
	├── job-manager
	│   └── kvs
	│       ├── content
	│       │   └── content-backing (content-sqlite)
	│       └── heartbeat
	└── resource
	    └── kvs-watch
	EOF
	test_debug "cat list-depends.out" &&
	test_cmp list-depends.expected list-depends.out
'

test_expect_success 'modprobe can list dependencies for modules (full)' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe list-dependencies --full kvs >full-depends.out &&
	cat <<-EOF > full-depends.expected &&
	kvs
	├── content
	│   ├── heartbeat
	│   └── content-backing (content-sqlite)
	└── heartbeat
	EOF
	test_debug "cat full-depends.out" &&
	test_cmp full-depends.expected full-depends.out
'
test_expect_success 'modprobe list-dependencies issues error for invalid task' '
	test_expect_code 1 flux modprobe list-dependencies foo
'
test_expect_success 'modprobe loads config from modprobe.d/*.toml' '
	mkdir modprobe.d &&
	test_when_finished "rm -rf modprobe.d" &&
	cat <<-EOF >modprobe.d/test.toml &&
	[[modules]]
	name = "test"
	requires = ["heartbeat"]
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe list-dependencies test \
		> test-deps.out &&
	test_debug "cat test-deps.out" &&
	grep test test-deps.out
'
test_expect_success 'modprobe load: fails with nonexistent module' '
	test_expect_code 1 flux modprobe load foo
'
test_expect_success 'modprobe load: fails with not loaded module' '
	test_expect_code 1 flux modprobe remove foo
'
test_expect_success 'modprobe remove complains about module dependencies' '
	test_expect_code 1 flux modprobe remove kvs 2>remove-kvs.err &&
	grep "kvs still in use" remove-kvs.err
'
test_expect_success 'modprobe show works' '
	flux modprobe show sched | jq  &&
	flux modprobe show sched | jq -e ".name == \"sched-simple\""
'
test_expect_success 'modprobe.toml can update modules' '
	mkdir modprobe.d &&
	test_when_finished "rm -rf modprobe.d" &&
	cat <<-EOF >modprobe.d/test.toml &&
	feasibility.ranks = "0,1"
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show feasibility | jq &&
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show feasibility \
		| jq -e ".ranks == \"0,1\""
'
test_expect_success 'modprobe applies updates from [modules] table' '
	flux config load <<-EOF &&
	[modules]
	feasibility.ranks = "0,4"
	EOF
	test_when_finished "echo {} | flux config load" &&
	flux modprobe show feasibility | jq &&
	flux modprobe show feasibility | jq -e ".ranks == \"0,4\""
'
test_done
