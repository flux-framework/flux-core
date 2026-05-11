#!/bin/sh

test_description='Test flux schedbench'

. $(dirname $0)/sharness.sh

test_under_flux 1

test_expect_success 'schedbench run: throughput writes results file' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=20 \
		--tag=test-throughput \
		--results-file=throughput.json &&
	test -f throughput.json &&
	grep -q "\"test_name\": \"throughput\"" throughput.json &&
	grep -q "\"tag\": \"test-throughput\"" throughput.json &&
	grep -q "\"njobs\": 20" throughput.json
'

test_expect_success 'schedbench run uses default -N=4' '
	flux schedbench run throughput \
		--cores-per-node=2 --gpus-per-node=0 \
		--njobs=10 --results-file=default-n.json &&
	grep -q "\"nodes\": 4" default-n.json
'

test_expect_success 'schedbench run records full resource shape' '
	flux schedbench run throughput \
		-N 8 --cores-per-node=4 --gpus-per-node=2 \
		--njobs=20 --results-file=shape.json &&
	grep -q "\"nodes\": 8" shape.json &&
	grep -q "\"cores_per_node\": 4" shape.json &&
	grep -q "\"gpus_per_node\": 2" shape.json
'

# FLUX_SCHEDBENCH_ISOLATED=1 is the recursion guard: when the
# launcher re-execs schedbench inside the fake-resources
# subinstance, it sets this env var so the inner invocation runs
# in the broker rather than launching another subinstance. We
# verify the guard works by setting the var externally — the
# resulting run executes in test_under_flux's 1-node broker
# rather than the 4-node subinstance --N would otherwise create.
test_expect_success 'FLUX_SCHEDBENCH_ISOLATED=1 skips subinstance launch' '
	FLUX_SCHEDBENCH_ISOLATED=1 flux schedbench run throughput \
		--njobs=4 --no-save --ui=off >isolated.out &&
	grep -q "\"nodes\": 1" isolated.out
'

# The fill-machine sharness sub-cases below cover two resource
# shapes: a cores-only run for the baseline cancel-and-cleanup
# path, and a 1-GPU-per-node variant. The GPU variant catches a
# regression in `flux R encode`'s idset argument formatting that
# previously produced R without GPUs whenever gpus_per_node was
# 1 (the degenerate `-g 0-0` range failed to parse). See the
# IDset usage in fake_resources.build_R_encode_args for the
# fix. t6010 has the in-process GPU allocation regression test.
test_expect_success 'schedbench run: fill-machine writes results file' '
	flux schedbench run fill-machine \
		-N 2 --cores-per-node=2 \
		--gpus-per-node=0 \
		--slot-cores=1 \
		--results-file=fillmachine.json &&
	test -f fillmachine.json &&
	grep -q "\"test_name\": \"fill-machine\"" fillmachine.json &&
	grep -q "\"njobs\": 4" fillmachine.json
'

test_expect_success 'schedbench run: fill-machine with 1 GPU/node' '
	flux schedbench run fill-machine \
		-N 2 --cores-per-node=2 \
		--gpus-per-node=1 \
		--slot-cores=1 --slot-gpus=1 \
		--results-file=fillmachine-gpu.json &&
	test -f fillmachine-gpu.json &&
	grep -q "\"test_name\": \"fill-machine\"" fillmachine-gpu.json &&
	grep -q "\"njobs\": 2" fillmachine-gpu.json
'

test_expect_success 'schedbench run --no-save skips results file' '
	rm -f no-save.json &&
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=10 --no-save --results-file=no-save.json &&
	test ! -e no-save.json
'

test_expect_success 'schedbench run --scheduler-options stored in record' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--scheduler-options="queue-depth=42" \
		--njobs=10 --results-file=sched-opts.json &&
	grep -q "\"options\": \"queue-depth=42\"" sched-opts.json
'

# --hwloc-xml-path and --amend-r are convenience flags that
# translate to --conf=fake-resources.* on the launched subinstance,
# and are passed through to the inner schedbench invocation so they
# appear in the results record (so reports can show which run used
# which topology / amender). Tests capture local hwloc XML via
# hwloc-ls since flux R encode rejects hand-written minimal XML.

test_expect_success 'capture local hwloc topology for schedbench tests' '
	if command -v hwloc-ls >/dev/null 2>&1; then
		hwloc-ls --of xml local.xml &&
		test_set_prereq HAVE_LSTOPO
	fi
'

test_expect_success HAVE_LSTOPO \
	'schedbench run --hwloc-xml-path replicates topology + records path' '
	flux schedbench run throughput \
		-N 4 --hwloc-xml-path=$(pwd)/local.xml \
		--njobs=10 --results-file=hwloc.json &&
	grep -q "\"hwloc_xml_path\":" hwloc.json &&
	grep -q "local.xml" hwloc.json
'

test_expect_success HAVE_LSTOPO \
	'schedbench run --amend-r runs amender and records spec' '
	cat <<-EOT >schedbench_amender.py &&
		def amend(R, hwloc_xml=None):
		    R["schedbench_marker"] = "amended"
		    return R
	EOT
	flux schedbench run throughput \
		-N 4 --hwloc-xml-path=$(pwd)/local.xml \
		--amend-r=$(pwd)/schedbench_amender.py \
		--njobs=10 --results-file=amend.json &&
	grep -q "\"amend_r\":" amend.json &&
	grep -q "schedbench_amender.py" amend.json
'

#
# Locality benchmark sub-cases. These verify the locality
# benchmark's plumbing (CLI options, duration modes, required
# arguments) and that the predicate produces a score in [0, 1] for
# real placements. The math is unit-tested in t6031; here we
# check end-to-end integration with the broker, scheduler, and
# results pipeline. A trivial-topology case asserts a specific
# score (1.0 on a single-NUMA topology where any placement is
# necessarily local) to catch silent regressions in the predicate
# integration.
#

test_expect_success HAVE_LSTOPO \
	'schedbench run: locality writes results file with score' '
	flux schedbench run locality \
		-N 4 --hwloc-xml-path=$(pwd)/local.xml \
		--njobs=10 --tag=test-locality \
		--results-file=locality.json &&
	test -f locality.json &&
	grep -q "\"test_name\": \"locality\"" locality.json &&
	grep -q "\"tag\": \"test-locality\"" locality.json &&
	grep -q "\"mean_locality_score\":" locality.json &&
	grep -q "\"num_jobs_scored\":" locality.json &&
	grep -q "\"locality_score_distribution\":" locality.json
'

test_expect_success \
	'schedbench run: locality requires --hwloc-xml-path' '
	test_must_fail flux schedbench run locality \
		-N 4 --njobs=4 --no-save --ui=off 2>locality-no-xml.out &&
	grep -qi "hwloc.xml.path" locality-no-xml.out
'

test_expect_success HAVE_LSTOPO \
	'schedbench run: locality --duration=fill saturates and cancels' '
	flux schedbench run locality --duration=fill \
		-N 4 --hwloc-xml-path=$(pwd)/local.xml \
		--no-save --ui=off >locality-fill.out &&
	grep -q "\"name\": \"result\"" locality-fill.out &&
	grep -q "\"mean_locality_score\":" locality-fill.out &&
	grep -q "\"duration\": \"fill\"" locality-fill.out
'

test_expect_success HAVE_LSTOPO \
	'schedbench run: locality --duration=random accepts range' '
	flux schedbench run locality --duration=random:0.1s-0.3s \
		-N 2 --hwloc-xml-path=$(pwd)/local.xml \
		--njobs=4 --no-save --ui=off >locality-rand.out &&
	grep -q "\"name\": \"result\"" locality-rand.out &&
	grep -q "\"duration\": \"random:0.1s-0.3s\"" locality-rand.out
'

test_expect_success HAVE_LSTOPO \
	'schedbench run: locality on single-NUMA topology scores 1.0' '
	hwloc-ls -i "package:1 numa:1 core:4 pu:1" --of xml --no-io \
		>single-numa.xml &&
	flux schedbench run locality \
		-N 4 --hwloc-xml-path=$(pwd)/single-numa.xml \
		--slot-cores=1 --njobs=8 \
		--results-file=single-numa.json &&
	grep -qE "\"mean_locality_score\":\s*1\.0+" single-numa.json
'

test_expect_success HAVE_LSTOPO \
	'schedbench run: locality --nslots groups multiple slots per job' '
	flux schedbench run locality \
		-N 4 --hwloc-xml-path=$(pwd)/local.xml \
		--nslots=2 --slot-cores=1 --njobs=4 \
		--results-file=locality-nslots.json &&
	grep -q "\"nslots\": 2" locality-nslots.json
'

# --extra-start-options is shlex-parsed and passed verbatim to
# the underlying flux start. We don't have a direct way from
# inside schedbench to confirm the broker attribute was set, so
# this is an indirect test: a typo in the launcher's argv
# construction (e.g. forgetting shlex.split, or quoting wrong)
# would cause flux start to reject the option and broker startup
# to fail. Reaching the result event means the option was
# accepted.
test_expect_success 'schedbench run --extra-start-options passes through' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--extra-start-options="--setattr=log-stderr-level=6" \
		--njobs=10 --no-save --ui=off \
		>extra.out &&
	grep -q "\"name\": \"result\"" extra.out
'

# --conf is a first-class accumulating flag that forwards each
# KEY=VALUE to `flux start --conf=KEY=VALUE`. Same indirect
# acceptance test as --extra-start-options: reaching the result
# event means flux start accepted the config.
test_expect_success 'schedbench run --conf passes through' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--conf=resource.noverify=true \
		--njobs=10 --no-save --ui=off \
		>conf.out &&
	grep -q "\"name\": \"result\"" conf.out
'

test_expect_success 'schedbench run --conf accumulates across multiple uses' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--conf=resource.noverify=true \
		--conf=resource.norestrict=true \
		--njobs=10 --no-save --ui=off \
		>conf-multi.out &&
	grep -q "\"name\": \"result\"" conf-multi.out
'

# --exec sub-cases:

test_expect_success 'schedbench run throughput --exec succeeds' '
	flux schedbench run throughput --exec \
		--njobs=4 --no-save --ui=off >exec-throughput.out &&
	grep -q "\"name\": \"result\"" exec-throughput.out &&
	grep -q "\"real_exec\":" exec-throughput.out
'

test_expect_success 'schedbench run throughput --exec records real_exec=true' '
	rm -f exec.json &&
	flux schedbench run throughput --exec \
		--njobs=4 --results-file=exec.json --ui=off >/dev/null &&
	grep -q "\"real_exec\": true" exec.json
'

test_expect_success 'schedbench run fill-machine --exec succeeds' '
	flux schedbench run fill-machine --exec \
		--no-save --ui=off >exec-fm.out &&
	grep -q "\"name\": \"result\"" exec-fm.out &&
	grep -q "time_to_fill" exec-fm.out
'

test_expect_success 'schedbench report renders REAL column' '
	flux schedbench report throughput \
		--results-file=exec.json >exec-report.out &&
	head -1 exec-report.out | grep -q "REAL"
'

# UI-mode coverage. Sharness redirects stdout to a file so
# isatty() is false during these tests; --ui=auto correctly
# falls back to JSON. --ui=off makes the fallback explicit
# (covers the path users will take when piping to jq), and
# --ui=on forces the terminal renderer even on a non-TTY so
# we exercise the rendering path itself. Color is disabled to
# keep the substring grep robust against escape sequences.
test_expect_success 'schedbench run --ui=off emits JSON event stream' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=4 --no-save \
		--ui=off >ui-off.out &&
	grep -q "\"name\": \"test.start\"" ui-off.out &&
	grep -q "\"name\": \"test.complete\"" ui-off.out
'

test_expect_success 'schedbench run --ui=on renders terminal block' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=4 --no-save \
		--ui=on --color=never >ui-on.out &&
	grep -q "flux schedbench" ui-on.out &&
	grep -q "throughput" ui-on.out &&
	grep -q "elapsed" ui-on.out
'

test_expect_success "schedbench run --quiet --ui=on shows results without progress" '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=4 --no-save \
		--ui=on --quiet >ui-quiet.out 2>ui-quiet.err &&
	grep -q "flux schedbench" ui-quiet.out &&
	grep -q "throughput" ui-quiet.out &&
	test_must_fail grep -q "█" ui-quiet.out
'

test_expect_success "schedbench run --quiet suppresses INFO log messages" '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=4 --no-save --quiet \
		>quiet-info.out 2>quiet-info.err &&
	test_must_fail grep -q "^flux-schedbench: INFO:" quiet-info.err &&
	test_must_fail grep -q "^flux-schedbench: INFO:" quiet-info.out
'

test_expect_success "schedbench run --quiet non-TTY emits single JSON object" '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=4 --no-save --quiet --ui=off \
		>quiet-json.out 2>&1 &&
	test "$(wc -l <quiet-json.out)" -eq 1 &&
	grep -q "\"throughput\":" quiet-json.out &&
	python3 -c "import json,sys; json.loads(open(\"quiet-json.out\").read())"
'

test_expect_success 'schedbench report TEST prints a table header' '
	flux schedbench report throughput \
		--results-file=throughput.json >report.out &&
	head -1 report.out | grep -q "SCHED" &&
	head -1 report.out | grep -q "JOBS"
'

test_expect_success "schedbench report TEST prints headline metric column" '
	flux schedbench report throughput \
		--results-file=throughput.json >report.out &&
	head -1 report.out | grep -q "THRPUT"
'

test_expect_success "schedbench report -o long includes extra columns" '
	flux schedbench report throughput -o long \
		--results-file=throughput.json >long.out &&
	head -1 long.out | grep -q "ALLOC"
'

test_expect_success "schedbench report -o csv emits comma-separated output" '
	flux schedbench report throughput -o csv \
		--results-file=throughput.json >csv.out &&
	head -1 csv.out | grep -q "," &&
	head -1 csv.out | grep -q "SCHED"
'

test_expect_success "schedbench report --no-header skips header row" '
	flux schedbench report throughput --no-header \
		--results-file=throughput.json >noheader.out &&
	head -1 noheader.out >noheader-first.out &&
	test_must_fail grep -q "SCHED" noheader-first.out
'

test_expect_success "schedbench report --filter on top-level key" '
	flux schedbench report throughput \
		--results-file=throughput.json \
		--filter=tag=test-throughput \
		>match-top.out &&
	grep -q "sched-simple" match-top.out
'

test_expect_success "schedbench report --filter on nested key (scheduler.name)" '
	flux schedbench report throughput \
		--results-file=throughput.json \
		--filter=scheduler.name=sched-simple \
		>match-nested.out &&
	grep -q "sched-simple" match-nested.out
'

test_expect_success "schedbench report --filter on doubly-nested key (resources.nodes)" '
	flux schedbench report throughput \
		--results-file=throughput.json \
		--filter=resources.nodes=4 \
		>match-deep.out &&
	grep -q "sched-simple" match-deep.out
'

test_expect_success "schedbench report --filter no-match exits nonzero" '
	test_must_fail flux schedbench report throughput \
		--results-file=throughput.json \
		--filter=tag=nonexistent-tag \
		>no-match.out 2>&1 &&
	grep -q "no .* match" no-match.out
'

test_expect_success "schedbench report --filter missing intermediate key" '
	test_must_fail flux schedbench report throughput \
		--results-file=throughput.json \
		--filter=scheduler.bogus=anything \
		>missing-key.out 2>&1 &&
	grep -q "no .* match" missing-key.out
'

# --sort and multi-filter need at least two distinct rows to
# exercise meaningfully. Create them in their own results file
# with distinct tags and njobs values so the sort order is
# unambiguous and the multi-filter case has a row to retain
# (multi-A) and a row to exclude (multi-B).
test_expect_success "schedbench report --sort orders rows by metric" '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--tag=multi-A --njobs=10 \
		--results-file=multi.json &&
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 --gpus-per-node=0 \
		--tag=multi-B --njobs=30 \
		--results-file=multi.json &&
	flux schedbench report throughput \
		--results-file=multi.json --sort=njobs \
		>sorted.out &&
	grep -n "multi-A\|multi-B" sorted.out >order.out &&
	head -1 order.out | grep -q "multi-A"
'

test_expect_success "schedbench report multiple --filter args combine" '
	flux schedbench report throughput \
		--results-file=multi.json \
		--filter=tag=multi-A \
		--filter=watcher=journal \
		>multi-filter.out &&
	grep -q "multi-A" multi-filter.out &&
	test_must_fail grep -q "multi-B" multi-filter.out
'

test_expect_success "schedbench report missing file is an error" '
	test_must_fail flux schedbench report throughput \
		--results-file=does-not-exist.json
'

test_expect_success "schedbench report requires a TEST argument" '
	test_must_fail flux schedbench report \
		--results-file=throughput.json 2>noarg.out &&
	grep -q "required" noarg.out
'

test_expect_success 'schedbench run requires TEST argument' '
	test_must_fail flux schedbench run 2>missing-test.out &&
	grep -qi "required\|the following arguments" missing-test.out
'

test_expect_success 'schedbench run: invalid benchmark name fails' '
	test_must_fail flux schedbench run no-such-benchmark -N 4
'

test_expect_success 'schedbench run --watcher=per-job runs and records watcher' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--watcher=per-job --njobs=20 \
		--results-file=perjob.json &&
	grep -q "\"watcher\": \"per-job\"" perjob.json
'

test_expect_success 'schedbench run --watcher=journal is the default' '
	flux schedbench run throughput \
		-N 4 --cores-per-node=2 \
		--gpus-per-node=0 \
		--njobs=20 \
		--results-file=journal.json &&
	grep -q "\"watcher\": \"journal\"" journal.json
'

test_expect_success 'schedbench run --watcher=bogus rejected at argparse' '
	test_must_fail flux schedbench run throughput \
		-N 4 --watcher=bogus
'

test_expect_success 'flux schedbench --help lists subcommands' '
	flux schedbench --help >help.out &&
	grep -q "run" help.out &&
	grep -q "sweep" help.out &&
	grep -q "report" help.out
'

#
# Sweep dispatch (flux schedbench sweep): exercises the parallel
# Flux-job dispatch path against the outer test broker.  Each child
# is a `flux schedbench run` invocation submitted as a 1-node job;
# the outer broker schedules them serially under test_under_flux 1.
#

test_expect_success 'schedbench sweep: basic CLI sweep writes records' '
	flux schedbench sweep throughput \
		--nodes=4,8 \
		--cores-per-node=2 --gpus-per-node=0 \
		--njobs=20 \
		--ui=off \
		--sweep-name=basic \
		--results-file=sweep-basic.json &&
	test -f sweep-basic.json &&
	grep -q "\"sweep\":" sweep-basic.json &&
	grep -q "\"name\": \"basic\"" sweep-basic.json
'

test_expect_success 'schedbench sweep: --from TOML loads sweep definition' '
	cat >sweep.toml <<-EOF &&
	name = "from-toml"
	test = "throughput"
	nodes = [4, 8]
	cores_per_node = 2
	gpus_per_node = 0
	njobs = 20
	EOF
	flux schedbench sweep --from=sweep.toml --ui=off \
		--results-file=sweep-toml.json &&
	test -f sweep-toml.json &&
	grep -q "\"name\": \"from-toml\"" sweep-toml.json
'

test_expect_success 'schedbench sweep: CLI overrides TOML param' '
	flux schedbench sweep --from=sweep.toml --ui=off \
		--njobs=10 \
		--sweep-name=cli-override \
		--results-file=sweep-override.json &&
	grep -q "\"njobs\": 10" sweep-override.json &&
	! grep -q "\"njobs\": 20" sweep-override.json
'

test_expect_success 'schedbench sweep: records carry sweep block' '
	# Each record must have sweep.{id,name,run_index,total,axes,fixed}
	# so the report tool can filter / group by sweep.
	grep -q "\"run_index\":" sweep-basic.json &&
	grep -q "\"axes\":" sweep-basic.json &&
	grep -q "\"total\": 2" sweep-basic.json
'

test_expect_success 'schedbench sweep: records carry config block' '
	# Per-test config block lets the report tool surface config-only
	# fields (notably njobs) for runs whose results are absent.
	grep -q "\"config\":" sweep-basic.json &&
	grep -q "\"results\":" sweep-basic.json
'

test_expect_success 'schedbench sweep: --no-save skips results file' '
	rm -f sweep-nosave.json &&
	flux schedbench sweep throughput \
		--nodes=4,8 \
		--cores-per-node=2 --gpus-per-node=0 \
		--njobs=20 \
		--ui=off --no-save \
		--results-file=sweep-nosave.json &&
	test ! -f sweep-nosave.json
'

test_expect_success 'schedbench sweep: bad TOML (env after subtable) rejected' '
	# TOML scoping pitfall: a key placed after [[scheduler.modules]]
	# attaches to the module entry, not the parent scheduler block.
	# The recipe parser hard-errors so the user sees the mistake
	# immediately rather than silently dropping the misplaced key.
	cat >bad-scope.toml <<-EOF &&
	test = "throughput"
	nodes = [4, 8]
	cores_per_node = 2
	gpus_per_node = 0
	njobs = 20

	[[scheduler]]
	name = "fluxion"

	[[scheduler.modules]]
	name = "sched-fluxion-resource"
	env = ["FOO=bar"]
	EOF
	test_must_fail flux schedbench sweep --from=bad-scope.toml \
		--ui=off --no-save 2>err.out &&
	grep -q "env" err.out
'

#
# Report rendering: empty / missing fields produce a single-hyphen
# marker for numeric columns (Flux empty-display convention), width-
# padded blank for string columns, and empty cells in CSV.  This is
# the failure path: a run that OOM-killed or otherwise died before
# emitting `result` leaves benchmarks[test].results empty.  Older
# records that predate a metric also exercise this path.
#

test_expect_success 'schedbench report handles record with empty results' '
	cat >missing-data.json <<-EOF &&
	{
	  "runs": [
	    {
	      "test_name": "fill-machine",
	      "iso_timestamp": "2026-05-19T00:00:00Z",
	      "tag": "", "watcher": "journal", "real_exec": false,
	      "scheduler": {"name": "sched-simple", "options": "",
	                    "version": null},
	      "resources": {"nodes": 4, "cores_per_node": 2,
	                    "gpus_per_node": 0,
	                    "hwloc_xml_path": "", "amend_r": ""},
	      "benchmarks": {
	        "fill-machine": {
	          "config": {"nodes": 4, "cores_per_node": 2,
	                     "gpus_per_node": 0,
	                     "scheduler": "sched-simple",
	                     "watcher": "journal", "real_exec": false,
	                     "njobs": 100, "slot_cores": 1,
	                     "slot_gpus": 0},
	          "results": {}
	        }
	      },
	      "error": "OOM killed"
	    }
	  ]
	}
	EOF
	flux schedbench report fill-machine \
		--results-file=missing-data.json >report.out 2>&1 &&
	# Identity columns from config are populated:
	grep -q "sched-simple" report.out &&
	grep -q "100" report.out &&
	# Numeric measurement columns render the hyphen marker:
	grep -q " -" report.out
'

test_expect_success 'schedbench report CSV empties missing cells' '
	# In CSV mode, missing values render as empty cells (no hyphen)
	# so spreadsheets and pandas see empty cells rather than a
	# "-" literal which would force a string dtype on the column.
	flux schedbench report fill-machine -o csv \
		--results-file=missing-data.json >report.csv 2>&1 &&
	# The data row should have empty cells where metrics are missing
	# — multiple consecutive commas (no value between them).
	grep -q ",," report.csv
'

test_done
