#!/bin/sh
#
test_description='Test job shell task placement with RFC 20 slots in R'

. `dirname $0`/sharness.sh

test_under_flux 4 job

# Print "<task rank>: <broker rank>" for each task sorted by task rank
cat <<'EOF' >map.sh
#!/bin/sh
echo $FLUX_TASK_RANK: $(flux getattr rank)
EOF
chmod +x map.sh

# Usage: make_R SLOTS0 SLOTS1
# Create R with 4 cores on rank 0 and 2 cores on rank 1, where SLOTSn is
# the slots object for rank n, or "none" to omit slots.
make_R() {
	flux R encode -r 0 -c 0-3 >r0.json &&
	flux R encode -r 1 -c 0-1 >r1.json &&
	cat r0.json r1.json | flux R append | \
	    jq -c --argjson s0 "$1" --argjson s1 "$2" '
		.execution.R_lite |= map(
		    if .rank == "0" then (if $s0 then .slots = $s0 else . end)
		    else (if $s1 then .slots = $s1 else . end) end)'
}

# Usage: run_total NTASKS R
# Run map.sh with a total task count of NTASKS using alloc-bypass R
run_total() {
	flux run --dry-run -n$1 --setattr=system.alloc-bypass.R="$2" \
	    ./map.sh | \
	    jq ".tasks[0].count = {\"total\": $1}" >total.json &&
	id=$(flux job submit total.json) &&
	flux job attach $id | sort -n
}

test_expect_success 'load alloc-bypass plugin' '
	flux jobtap load alloc-bypass.so
'
test_expect_success 'tasks are distributed evenly when R has no slots' '
	make_R null null >R.noslots &&
	flux run -n4 --setattr=system.alloc-bypass.R="$(cat R.noslots)" \
	    ./map.sh | sort -n >noslots.out &&
	cat <<-EOF >noslots.expected &&
	0: 0
	1: 0
	2: 1
	3: 1
	EOF
	test_cmp noslots.expected noslots.out
'
test_expect_success 'tasks follow slots in R' '
	make_R "{\"task\":{\"count\":3,\"core\":\"0-3\"}}" \
	    "{\"task\":{\"count\":1,\"core\":\"0-1\"}}" >R.slots &&
	flux run -n4 --setattr=system.alloc-bypass.R="$(cat R.slots)" \
	    ./map.sh | sort -n >slots.out &&
	cat <<-EOF >slots.expected &&
	0: 0
	1: 0
	2: 0
	3: 1
	EOF
	test_cmp slots.expected slots.out
'
test_expect_success 'fewer tasks than slots are placed round-robin' '
	run_total 2 "$(cat R.slots)" >slots-n2.out &&
	cat <<-EOF >slots-n2.expected &&
	0: 0
	1: 1
	EOF
	test_cmp slots-n2.expected slots-n2.out
'
test_expect_success 'more tasks than slots oversubscribe slots' '
	run_total 8 "$(cat R.slots)" >slots-n8.out &&
	test $(grep -c ": 0$" slots-n8.out) -eq 6 &&
	test $(grep -c ": 1$" slots-n8.out) -eq 2
'
test_expect_success 'per-resource option overrides slots in R' '
	flux run -n4 -o per-resource.type=node \
	    --setattr=system.alloc-bypass.R="$(cat R.slots)" \
	    ./map.sh | sort -n >per-resource.out &&
	cat <<-EOF >per-resource.expected &&
	0: 0
	1: 1
	EOF
	test_cmp per-resource.expected per-resource.out
'
for type in core node; do
	test_expect_success "per-resource $type with count > 1 matches R without slots" '
		flux run -n1 -o per-resource.type=$type -o per-resource.count=3 \
		    --setattr=system.alloc-bypass.R="$(cat R.noslots)" \
		    ./map.sh | sort -n >per-$type-noslots.out &&
		flux run -n1 -o per-resource.type=$type -o per-resource.count=3 \
		    --setattr=system.alloc-bypass.R="$(cat R.slots)" \
		    ./map.sh | sort -n >per-$type-slots.out &&
		test_cmp per-$type-noslots.out per-$type-slots.out
	'
done
test_expect_success 'per-resource core with count > 1 runs count tasks per core' '
	flux run -n1 -o per-resource.type=core -o per-resource.count=2 \
	    --setattr=system.alloc-bypass.R="$(cat R.slots)" \
	    ./map.sh | sort -n >per-core2.out &&
	test $(grep -c ": 0$" per-core2.out) -eq 8 &&
	test $(grep -c ": 1$" per-core2.out) -eq 4
'
test_expect_success 'per-resource node with count > 1 runs count tasks per node' '
	flux run -n1 -o per-resource.type=node -o per-resource.count=16 \
	    --setattr=system.alloc-bypass.R="$(cat R.slots)" \
	    ./map.sh | sort -n >per-node16.out &&
	test $(grep -c ": 0$" per-node16.out) -eq 16 &&
	test $(grep -c ": 1$" per-node16.out) -eq 16
'
test_expect_success 'tasks are placed in slots matching the jobspec label' '
	make_R "{\"cpu\":{\"count\":1,\"core\":\"0-1\"},\
\"other\":{\"count\":1,\"core\":\"2-3\"}}" \
	    "{\"cpu\":{\"count\":2,\"core\":\"0-1\"}}" >R.label &&
	flux run --dry-run -n3 \
	    --setattr=system.alloc-bypass.R="$(cat R.label)" ./map.sh | \
	    jq ".resources[0].label = \"cpu\" |
	        .tasks[0].slot = \"cpu\"" >label.json &&
	id=$(flux job submit label.json) &&
	flux job attach $id | sort -n >label.out &&
	cat <<-EOF >label.expected &&
	0: 0
	1: 1
	2: 1
	EOF
	test_cmp label.expected label.out
'
test_expect_success 'per_slot task count uses slots in R' '
	flux run -n1 --setattr=system.alloc-bypass.R="$(cat R.slots)" \
	    ./map.sh | sort -n >per-slot.out &&
	cat <<-EOF >per-slot.expected &&
	0: 0
	1: 0
	2: 0
	3: 1
	EOF
	test_cmp per-slot.expected per-slot.out
'
test_expect_success 'job with invalid slots in R fails' '
	make_R "{\"task\":{\"count\":1,\"core\":\"0-1\"}}" \
	    "{\"task\":{\"count\":1,\"core\":\"0-1\"}}" >R.bad &&
	test_must_fail flux run -n2 \
	    --setattr=system.alloc-bypass.R="$(cat R.bad)" \
	    ./map.sh 2>bad.err &&
	test_debug "cat bad.err" &&
	grep "children.core not fully assigned to slots" bad.err
'
PLUGINPATH="${SHARNESS_TEST_DIRECTORY}/shell/plugins/.libs"
test_expect_success 'create initrc to load slot-info plugin' '
	cat <<-EOF >slot-info.lua
	plugin.searchpath = "${PLUGINPATH}"
	plugin.load { file = "slot-info.so" }
	EOF
'
# Usage: slot_info R ARGS...
# Run a job with alloc-bypass R and the slot-info plugin, print sorted
# slot-info output with log prefixes removed.
slot_info() {
	R="$1" &&
	shift &&
	flux run -o userrc=slot-info.lua \
	    --setattr=system.alloc-bypass.R="$R" "$@" true 2>&1 | \
	    sed -n "s/^.*slot-info: //p" | sort
}
test_expect_success 'plugin API has no slots when R has no slots' '
	slot_info "$(cat R.noslots)" -n4 >info-noslots.out &&
	test_debug "cat info-noslots.out" &&
	cat <<-EOF >info-noslots.expected &&
	slots: none
	slots: none
	task 0 slot: none
	task 1 slot: none
	task 2 slot: none
	task 3 slot: none
	EOF
	test_cmp info-noslots.expected info-noslots.out
'
test_expect_success 'plugin API reports slots from R' '
	slot_info "$(cat R.slots)" -n4 >info-slots.out &&
	test_debug "cat info-slots.out" &&
	cat <<-EOF >info-slots.expected &&
	slots: [{"cores":"0-1","gpus":"","label":"task","ntasks":1,"taskids":"0"},{"cores":"2","gpus":"","label":"task","ntasks":1,"taskids":"1"},{"cores":"3","gpus":"","label":"task","ntasks":1,"taskids":"2"}]
	slots: [{"cores":"0-1","gpus":"","label":"task","ntasks":1,"taskids":"3"}]
	task 0 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":1,"task_index":0}
	task 1 slot: {"cores":"2","gpus":"","id":1,"label":"task","ntasks":1,"task_index":0}
	task 2 slot: {"cores":"3","gpus":"","id":2,"label":"task","ntasks":1,"task_index":0}
	task 3 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":1,"task_index":0}
	EOF
	test_cmp info-slots.expected info-slots.out
'
test_expect_success 'plugin API reports shared slots when oversubscribed' '
	flux run --dry-run -n8 -o userrc=slot-info.lua \
	    --setattr=system.alloc-bypass.R="$(cat R.slots)" true | \
	    jq ".tasks[0].count = {\"total\": 8}" >over.json &&
	id=$(flux job submit over.json) &&
	flux job attach $id 2>&1 | sed -n "s/^.*slot-info: //p" | \
	    grep "^task" | sort -n -k2 >info-over.out &&
	test_debug "cat info-over.out" &&
	grep "task 0 slot:.*\"ntasks\":2,\"task_index\":0" info-over.out &&
	grep "task 3 slot:.*\"ntasks\":2,\"task_index\":1" info-over.out &&
	grep "task 6 slot:.*\"cores\":\"0-1\",.*\"id\":0,.*\"task_index\":0" \
	    info-over.out
'
test_expect_success 'plugin API reports slot label and gpus' '
	flux R encode -r 0 -c 0-3 -g 0-1 | \
	    jq -c ".execution.R_lite[0].slots = {
		\"cpu\": {\"count\": 1, \"core\": \"0-1\"},
		\"gpu\": {\"count\": 2, \"core\": \"2-3\", \"gpu\": \"0-1\"}}" \
	    >R.gpu &&
	flux run --dry-run -n2 -o userrc=slot-info.lua \
	    --setattr=system.alloc-bypass.R="$(cat R.gpu)" true | \
	    jq ".resources[0].label = \"gpu\" | .tasks[0].slot = \"gpu\"" \
	    >gpu.json &&
	id=$(flux job submit gpu.json) &&
	flux job attach $id 2>&1 | sed -n "s/^.*slot-info: //p" | sort \
	    >info-gpu.out &&
	test_debug "cat info-gpu.out" &&
	cat <<-EOF >info-gpu.expected &&
	slots: [{"cores":"0-1","gpus":"","label":"cpu","ntasks":0,"taskids":""},{"cores":"2","gpus":"0","label":"gpu","ntasks":1,"taskids":"0"},{"cores":"3","gpus":"1","label":"gpu","ntasks":1,"taskids":"1"}]
	task 0 slot: {"cores":"2","gpus":"0","id":1,"label":"gpu","ntasks":1,"task_index":0}
	task 1 slot: {"cores":"3","gpus":"1","id":2,"label":"gpu","ntasks":1,"task_index":0}
	EOF
	test_cmp info-gpu.expected info-gpu.out
'
test_expect_success 'plugin API task slots follow a cyclic taskmap' '
	slot_info "$(cat R.slots)" -n4 --taskmap=cyclic >info-cyclic.out &&
	test_debug "cat info-cyclic.out" &&
	cat <<-EOF >info-cyclic.expected &&
	slots: [{"cores":"0-1","gpus":"","label":"task","ntasks":1,"taskids":"0"},{"cores":"2","gpus":"","label":"task","ntasks":1,"taskids":"2"},{"cores":"3","gpus":"","label":"task","ntasks":1,"taskids":"3"}]
	slots: [{"cores":"0-1","gpus":"","label":"task","ntasks":1,"taskids":"1"}]
	task 0 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":1,"task_index":0}
	task 1 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":1,"task_index":0}
	task 2 slot: {"cores":"2","gpus":"","id":1,"label":"task","ntasks":1,"task_index":0}
	task 3 slot: {"cores":"3","gpus":"","id":2,"label":"task","ntasks":1,"task_index":0}
	EOF
	test_cmp info-cyclic.expected info-cyclic.out
'
# Usage: run_map R TASKMAP
# Run 4 tasks of map.sh with alloc-bypass R and taskmap TASKMAP, and
# print "<task>: <broker rank>" followed by the per-task cpusets.
run_map() {
	flux run -n4 --taskmap="$2" --setattr=system.alloc-bypass.R="$1" \
	    -o cpu-affinity=dry-run,per-task ./map.sh 2>$3.err | sort -n &&
	grep -o "task [0-9]*: cpus: .*" $3.err | sort -n -k2
}
test_expect_success 'create R with even slots matching an even split' '
	flux R encode -r 0-1 -c 0-3 | \
	    jq -c ".execution.R_lite[0].slots = {
		\"task\": {\"count\": 2, \"core\": \"0-3\"}}" >R.even-slots &&
	flux R encode -r 0-1 -c 0-3 >R.even-noslots
'
for map in block cyclic cyclic:2 "manual:[[0,1,1,1],[1,1,1,1],[0,1,1,1],[1,1,1,1]]"
do
	test_expect_success "taskmap $map with slots matches R without slots" '
		run_map "$(cat R.even-noslots)" "$map" noslots >map-noslots.out &&
		run_map "$(cat R.even-slots)" "$map" slots >map-slots.out &&
		test_debug "cat map-noslots.out map-slots.out" &&
		test_cmp map-noslots.out map-slots.out
	'
done
test_expect_success 'uneven manual taskmap assigns tasks to slots of each rank' '
	slot_info "$(cat R.even-slots)" -n4 \
	    --taskmap="manual:[[1,1,3,1],[0,1,1,1]]" | \
	    grep "^task" | sort -n -k2 >map-uneven.out &&
	test_debug "cat map-uneven.out" &&
	cat <<-EOF >map-uneven.expected &&
	task 0 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":2,"task_index":0}
	task 1 slot: {"cores":"2-3","gpus":"","id":1,"label":"task","ntasks":1,"task_index":0}
	task 2 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":2,"task_index":1}
	task 3 slot: {"cores":"0-1","gpus":"","id":0,"label":"task","ntasks":1,"task_index":0}
	EOF
	test_cmp map-uneven.expected map-uneven.out
'
test_expect_success 'taskmap plugin with slots in R works' '
	cat <<-EOF >taskmap-initrc.lua &&
	plugin.searchpath = "${PLUGINPATH}"
	plugin.load { file = "taskmap-reverse.so" }
	EOF
	id=$(flux submit -n4 -o userrc=taskmap-initrc.lua --taskmap=reverse \
	    --setattr=system.alloc-bypass.R="$(cat R.even-slots)" ./map.sh) &&
	flux job attach $id | sort -n >map-reverse.out &&
	test "$(flux job taskmap $id)" = "[[1,1,2,1],[0,1,2,1]]" &&
	flux job taskmap --to=multiline $id >map-reverse.expected &&
	test_cmp map-reverse.expected map-reverse.out
'
#
# Multiple tasks bound to slot labels, as for RFC 14 "or" slots, where the
# scheduler chooses a label for each slot and the other labels are unused
#
test_expect_success 'create or slots test script' '
	cat <<-"EOF" >app.sh &&
	#!/bin/sh
	echo $FLUX_TASK_RANK: $1 $(flux getattr rank)
	EOF
	chmod +x app.sh
'
# Usage: or_jobspec R
# Create a jobspec with tasks app.sh alpha (slot alpha), app.sh beta (slot
# beta), and app.sh gamma (slot gamma), using alloc-bypass R.
or_jobspec() {
	flux run --dry-run -N2 --setattr=system.alloc-bypass.R="$1" \
	    ./app.sh alpha | \
	    jq ".resources[0].with[0].label = \"alpha\" |
	        .tasks = [
	          (.tasks[0] | .slot = \"alpha\"),
	          (.tasks[0] | .slot = \"beta\" | .command[1] = \"beta\"),
	          (.tasks[0] | .slot = \"gamma\" | .command[1] = \"gamma\")
	        ] |
	        .tasks[].count = {\"per_slot\": 1}"
}
# Usage: or_R SLOTS0 SLOTS1
# Create R with 4 cores on ranks 0 and 1 with slots SLOTS0 and SLOTS1
or_R() {
	flux R encode -r 0-1 -c 0-3 | \
	    jq -c --argjson s0 "$1" --argjson s1 "$2" "
		.execution.R_lite = [
		    (.execution.R_lite[0] | .rank = \"0\" | .slots = \$s0),
		    (.execution.R_lite[0] | .rank = \"1\" | .slots = \$s1)]"
}
test_expect_success 'or slots: each rank runs the task of its slot label' '
	or_R "{\"alpha\":{\"count\":2,\"core\":\"0-3\"}}" \
	    "{\"beta\":{\"count\":2,\"core\":\"0-3\"}}" >R.or &&
	or_jobspec "$(cat R.or)" >or.json &&
	id=$(flux job submit or.json) &&
	flux job attach $id | sort -n >or.out &&
	cat <<-EOF >or.expected &&
	0: alpha 0
	1: alpha 0
	2: beta 1
	3: beta 1
	EOF
	test_cmp or.expected or.out
'
test_expect_success 'or slots: tasks on one rank run the task of their slot' '
	or_R "{\"alpha\":{\"count\":1,\"core\":\"0-1\"},\
\"beta\":{\"count\":1,\"core\":\"2-3\"}}" \
	    "{\"gamma\":{\"count\":2,\"core\":\"0-3\"}}" >R.or-mixed &&
	or_jobspec "$(cat R.or-mixed)" >or-mixed.json &&
	id=$(flux job submit or-mixed.json) &&
	flux job attach $id | sort -n >or-mixed.out &&
	cat <<-EOF >or-mixed.expected &&
	0: alpha 0
	1: beta 0
	2: gamma 1
	3: gamma 1
	EOF
	test_cmp or-mixed.expected or-mixed.out
'
test_expect_success 'or slots: task slot info reports the slot label' '
	cat <<-EOF >or-info.lua &&
	plugin.searchpath = "${PLUGINPATH}"
	plugin.load { file = "slot-info.so" }
	EOF
	jq ".attributes.system.shell.options.userrc = \"or-info.lua\"" \
	    or-mixed.json >or-info.json &&
	id=$(flux job submit or-info.json) &&
	flux job attach $id 2>&1 | sed -n "s/^.*slot-info: //p" | \
	    grep "^task" | sort -n -k2 >or-info.out &&
	test_debug "cat or-info.out" &&
	grep "task 0 slot: .*\"label\":\"alpha\"" or-info.out &&
	grep "task 1 slot: .*\"label\":\"beta\"" or-info.out &&
	grep "task 2 slot: .*\"label\":\"gamma\"" or-info.out
'
test_expect_success 'or slots: cyclic taskmap runs the task of each slot' '
	jq ".attributes.system.shell.options.taskmap =
	    {\"scheme\": \"cyclic\", \"value\": \"\"}" \
	    or-mixed.json >or-cyclic.json &&
	id=$(flux job submit or-cyclic.json) &&
	flux job attach $id | sort -n >or-cyclic.out &&
	cat <<-EOF >or-cyclic.expected &&
	0: alpha 0
	1: gamma 1
	2: beta 0
	3: gamma 1
	EOF
	test_cmp or-cyclic.expected or-cyclic.out
'
test_expect_success 'or slots: without slots in R tasks run the first task' '
	or_jobspec "$(cat R.noslots)" >or-noslots.json &&
	id=$(flux job submit or-noslots.json) &&
	flux job attach $id | sort -n >or-noslots.out &&
	test_debug "cat or-noslots.out" &&
	test $(grep -c ": alpha " or-noslots.out) -eq 2
'
test_expect_success 'or slots: slot label not in tasks fails with an error' '
	or_R "{\"alpha\":{\"count\":2,\"core\":\"0-3\"}}" \
	    "{\"delta\":{\"count\":2,\"core\":\"0-3\"}}" >R.or-bad &&
	or_jobspec "$(cat R.or-bad)" >or-bad.json &&
	id=$(flux job submit or-bad.json) &&
	test_must_fail flux job attach $id 2>or-bad.err &&
	test_debug "cat or-bad.err" &&
	grep "no task in jobspec for slot label delta" or-bad.err
'
test_expect_success 'or slots: single task with labels from flux-sched or_slot' '
	or_R "{\"small\":{\"count\":2,\"core\":\"0-3\"}}" \
	    "{\"big\":{\"count\":1,\"core\":\"0-3\"}}" >R.or-sched &&
	flux run -N2 -n3 --setattr=system.alloc-bypass.R="$(cat R.or-sched)" \
	    ./app.sh default | sort -n >or-sched.out &&
	test_debug "cat or-sched.out" &&
	cat <<-EOF >or-sched.expected &&
	0: default 0
	1: default 0
	2: default 1
	EOF
	test_cmp or-sched.expected or-sched.out
'
test_expect_success 'or slots: tasks sharing a slot label with slots fail' '
	jq ".tasks[1].slot = \"alpha\"" or.json >shared.json &&
	id=$(flux job submit shared.json) &&
	test_must_fail flux job attach $id 2>shared.err &&
	test_debug "cat shared.err" &&
	grep "multiple tasks bound to slot label alpha are not supported" \
	    shared.err
'
test_expect_success 'or slots: tasks sharing an unassigned slot label work' '
	jq ".tasks[2].slot = \"beta\"" or.json >shared-unused.json &&
	or_R "{\"alpha\":{\"count\":2,\"core\":\"0-3\"}}" \
	    "{\"alpha\":{\"count\":2,\"core\":\"0-3\"}}" >R.or-alpha &&
	jq ".attributes.system[\"alloc-bypass\"].R = $(cat R.or-alpha)" \
	    shared-unused.json >shared-unused2.json &&
	id=$(flux job submit shared-unused2.json) &&
	flux job attach $id | sort -n >shared-unused.out &&
	test_debug "cat shared-unused.out" &&
	test $(grep -c ": alpha " shared-unused.out) -eq 4
'
test_expect_success 'single task with a different slot label runs anywhere' '
	flux run -N2 --setattr=system.alloc-bypass.R="$(cat R.or)" \
	    ./app.sh one | sort -n >one.out &&
	test_debug "cat one.out" &&
	test $(grep -c ": one " one.out) -eq 4
'
test_expect_success 'unload alloc-bypass plugin' '
	flux jobtap remove alloc-bypass.so
'
test_done
