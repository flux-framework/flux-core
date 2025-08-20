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
test_expect_success 'modprobe detects invalid task args' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
	@task("test", branks="0")
	def task(context):
	    print("ran a task")
	EOF
	test_must_fail flux modprobe run test${seq}.py >output${seq} 2>&1 &&
	test_debug "cat output${seq}" &&
	grep "test: unknown task argument branks" output${seq}
'
seq=$((seq=seq+1))
test_expect_success 'modprobe task name is required' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task
	@task
	def task(context):
	    print("ran a task")
	EOF
	test_must_fail flux modprobe run test${seq}.py >output${seq} 2>&1 &&
	test_debug "cat output${seq}" &&
	grep "missing required name argument" output${seq}
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
test_expect_success 'modprobe after=* works' '
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

	@task("really-last", after=["*"])
	def really_last(context):
	    print("really-last")
	EOF
	flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}" &&
	cat <<-EOF >test${seq}.expected &&
	setup
	first
	last
	really-last
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
test_expect_success 'modprobe ranks detects bad input' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first")
	def first(context):
	    print("first")

	@task("last", ranks="=0")
	def last(context):
	    print("last")
	EOF
	test_must_fail flux modprobe run test${seq}.py >output${seq} 2>&1 &&
	test_debug "cat output${seq}" &&
	grep "invalid idset" output${seq}
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
test_expect_success 'modprobe ranks works with <' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("last", ranks="<1")
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
test_expect_success 'modprobe ranks=0 runs task on rank 0' '
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
seq=$((seq=seq+1))
test_expect_success 'modprobe fails if bash() task fails' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("next")
	def needed(context):
	    context.bash("false")
	EOF
	test_must_fail flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}"
'
seq=$((seq=seq+1))
test_expect_success 'modprobe fails if task raises exception' '
	cat <<-EOF >test${seq}.py &&
	from flux.modprobe import task

	@task("first", before=["*"])
	def first(context):
	    print("first")

	@task("next")
	def needed(context):
	   raise RuntimeException("test exception")
	EOF
	test_must_fail flux modprobe run test${seq}.py >output${seq} &&
	test_debug "cat output${seq}"
'
test_expect_success 'modprobe: detects missing required modprobe.toml keys' '
	cat <<-EOF >missing.toml &&
	[[modules]]
	name = "a"
	[[modules]]
	ranks = "0"
	EOF
	test_must_fail flux modprobe show --path=missing.toml a 2>missing.err &&
	test_debug "cat missing.err" &&
	grep -i "missing required config key" missing.err
'
test_expect_success 'modprobe: detects invalid modprobe.toml entries' '
	cat <<-EOF >invalid.toml &&
	[[modules]]
	name = "a"
	badkey = ""
	EOF
	test_must_fail flux modprobe show --path=invalid.toml a 2>invalid.err &&
	test_debug "cat invalid.err" &&
	grep -i "invalid config key" invalid.err
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
test_expect_success 'modprobe load: succeeds if module is already loaded' '
	flux modprobe load sched 2>already-loaded.err &&
	test_debug "cat already-loaded.err" &&
	grep "All modules.*already loaded" already-loaded.err
'
test_expect_success 'modprobe load: fails with not loaded module' '
	test_expect_code 1 flux modprobe remove foo
'
test_expect_success 'modprobe remove complains about module dependencies' '
	test_expect_code 1 flux modprobe remove kvs 2>remove-kvs.err &&
	grep "kvs still in use" remove-kvs.err
'
test_expect_success 'modprobe remove all works' '
	flux start sh -c "flux modprobe remove all && flux module list && flux modprobe load sched" > remove-all.out &&
	test_debug "cat remove-all.out" &&
	test_must_fail grep sched remove-all.out &&
	test_must_fail grep kvs remove-all.out
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
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show feasibility > show1.json &&
	test_debug "cat show1.json | jq" &&
	cat show1.json | jq -e ".ranks == \"0,1\""
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
test_expect_success 'create a couple alternative schedulers' '
	mkdir modprobe.d &&
	cat <<-EOF >modprobe.d/000.toml &&
	[[modules]]
	name = "advanced-scheduler"
	priority = 500
	ranks = "0"
	provides = ["sched", "feasibility"]
	requires = ["job-manager", "resource"]
	EOF
	cat <<-EOF >modprobe.d/001.toml
	[[modules]]
	name = "basic-scheduler"
	ranks = "0"
	provides = ["sched", "feasibility"]
	requires = ["job-manager", "resource"]
	EOF
'
test_expect_success 'modprobe module priority works' '
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show sched > sched1.json &&
	cat sched1.json | jq -e ".name == \"advanced-scheduler\"" &&
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show feasibility > feas1.json &&
	cat feas1.json | jq -e ".name == \"advanced-scheduler\""
'
test_expect_success 'modprobe: disable detects invalid module' '
	FLUX_MODPROBE_PATH=$(pwd) \
		test_must_fail flux modprobe show --disable=foo sched
'
test_expect_success 'modprobe: disabling module results in other alternative' '
	FLUX_MODPROBE_PATH=$(pwd) \
		flux modprobe show \
		    --disable=advanced-scheduler sched > sched.dis.json &&
	cat sched.dis.json | jq -e ".name == \"basic-scheduler\"" &&
	FLUX_MODPROBE_PATH=$(pwd) \
		flux modprobe show \
		    --disable=advanced-scheduler feasibility > feas.dis.json &&
	cat feas.dis.json | jq -e ".name == \"basic-scheduler\""
'
test_expect_success 'modprobe: disabling 2 modules results in final alternative' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show \
	        --disable=advanced-scheduler,basic-scheduler sched \
	        > sched.dis2.json &&
	cat sched.dis2.json | jq -e ".name == \"sched-simple\"" &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show \
	        --disable=advanced-scheduler,basic-scheduler feasibility \
	        > feas.dis2.json &&
	cat feas.dis2.json | jq -e ".name == \"sched-simple\""
'
test_expect_success 'modprobe: FLUX_MODPROBE_DISABLE works' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    FLUX_MODPROBE_DISABLE=advanced-scheduler,basic-scheduler \
	    flux modprobe show sched \
	        > sched.dis3.json &&
	cat sched.dis3.json | jq -e ".name == \"sched-simple\"" &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    FLUX_MODPROBE_DISABLE=advanced-scheduler,basic-scheduler \
	    flux modprobe show feasibility \
	        > feas.dis3.json &&
	cat feas.dis3.json | jq -e ".name == \"sched-simple\""
'
# Note: below test will fail if FLUX_MODPROBE_DISABLE is not working since
# configuration contains test modules that do not actually exist:
test_expect_success 'modprobe: FLUX_MODPROBE_DISABLE works in rc1' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    FLUX_MODPROBE_DISABLE=advanced-scheduler,basic-scheduler \
		FLUX_RC_USE_MODPROBE=t \
		    flux start flux module list | grep sched-simple
'
test_expect_success 'modprobe: --set-alternative detects bad input' '
	FLUX_MODPROBE_PATH=$(pwd) \
		test_must_fail \
			flux modprobe show --set-alternative=sched sched
'
test_expect_success 'modprobe: set_alternative() works' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show --set-alternative=sched=basic-scheduler \
	        sched > sched2.json &&
	cat sched2.json | jq -e ".name == \"basic-scheduler\"" &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show --set-alternative=feasibility=basic-scheduler \
	        feasibility > feas2.json &&
	cat feas2.json | jq -e ".name == \"basic-scheduler\""
'
test_expect_success 'modprobe: alternatives table in broker config works' '
	cat <<-EOF | flux config load &&
	[modules.alternatives]
	sched = "basic-scheduler"
	feasibility = "basic-scheduler"
	EOF
	flux config get | jq &&
	test_when_finished "echo {} | flux config load" &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show sched > sched3.json &&
	cat sched3.json | jq -e ".name == \"basic-scheduler\"" &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show feasibility > feas3.json &&
	cat feas3.json | jq -e ".name == \"basic-scheduler\""
'
test_expect_success 'modprobe: priority can be updated via TOML file' '
	cat <<-EOF > modprobe.d/999.toml &&
	basic-scheduler.priority = 1000
	EOF
	test_when_finished "rm modprobe.d/999.toml" &&
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe show basic-scheduler | jq &&
	FLUX_MODPROBE_PATH=$(pwd) \
	    flux modprobe show sched > sched4.json &&
	cat sched4.json | jq -e ".name == \"basic-scheduler\""
'
test_expect_success 'modprobe: set_alternative() detects invalid module' '
	FLUX_MODPROBE_PATH=$(pwd) \
	    test_must_fail \
	        flux modprobe show --set-alternative=sched=foo sched \
		    > badalt.out 2>&1 &&
	test_debug "cat badalt.out" &&
	grep "no module foo provides sched" badalt.out
'
test_expect_success 'remove temporary configuration' '
	rm -rf modprobe.d
'
test_expect_success 'modprobe: dry-run works' '
	flux modprobe rc1 --dry-run >rc1.out &&
	test_debug "cat rc1.out" &&
	grep "load kvs" rc1.out &&
	flux modprobe rc3 --dry-run >rc3.out &&
	test_debug "cat rc3.out" &&
	grep "remove kvs" rc3.out
'
test_expect_success 'modprobe: module arguments can be appended' '
	mkdir -p modprobe.d &&
	mkdir -p rc1.d &&
	cat <<-EOF >modprobe.d/test.toml &&
	[[modules]]
	name = "test-module"
	args = [ "foo", "bar" ]
	EOF
	cat <<-EOF >rc1.d/01.py &&
	def setup(context):
	    context.setopt("test-module", "test-arg")
	    context.load_modules(["test-module"])
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe rc1 --dry-run >append.out &&
	test_debug "cat append.out" &&
	grep "test-module foo bar test-arg" append.out
'
test_expect_success 'modprobe: setopt() splits on space by default' '
	cat <<-EOF >rc1.d/01.py &&
	def setup(context):
	    context.setopt("test-module", "test-arg    additional")
	    context.load_modules(["test-module"])
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe rc1 --dry-run >append2.out &&
	test_debug "cat append2.out" &&
	grep "test-module foo bar test-arg additional" append2.out
'
test_expect_success 'modprobe: module arguments can be overwritten' '
	cat <<-EOF >rc1.d/01.py &&
	def setup(context):
	    context.setopt("test-module", "test-arg", overwrite=True)
	    context.load_modules(["test-module"])
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux modprobe rc1 --dry-run >overwrite.out &&
	test_debug "cat overwrite.out" &&
	grep "test-module test-arg" overwrite.out
'
test_expect_success 'modprobe: remove test config' '
	rm -rf rc1.d modprobe.d
'
test_expect_success 'modprobe: FLUX_SCHED_MODULE=none disables scheduler' '
	FLUX_SCHED_MODULE=none flux modprobe rc1 --dry-run >no-sched.out &&
	test_debug "cat no-sched.out" &&
	test_must_fail grep sched-simple no-sched.out
'
test_expect_success 'modprobe: context.enable() can force-enable module' '
	mkdir rc1.d &&
	test_when_finished "rm -rf rc1.d" &&
	cat <<-EOF >rc1.d/x.py &&
	def setup(context):
	    # Enable module known to only reside on rank 0
	    context.enable("feasibility")
	EOF
	FLUX_MODPROBE_PATH=$(pwd) flux exec -r 1 \
	    flux modprobe rc1 --dry-run >enable.out &&
	test_debug "cat enable.out" &&
	grep sched-simple enable.out
'
test_expect_success 'modprobe: fails if module load fails' '
	flux python <<-EOF >all_modules &&
	import flux
	from flux.modprobe import ModuleList
	modules = [x for x in ModuleList(flux.Flux())]
	print(" ".join(modules))
	EOF
	flux modprobe remove all &&
	test_when_finished "flux modprobe load $(cat all_modules)" &&
	cat <<-EOF >sched-badarg.py &&
	def setup(context):
	    context.load_modules(["sched-simple"])
	    context.setopt("sched-simple", "badarg")
	EOF
	test_must_fail flux modprobe run sched-badarg.py
'
test_done
