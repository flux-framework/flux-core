#!/bin/sh
#
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
#
# ci=asan
#

test_description='Test fake-resources modprobe rc1 task'

. $(dirname $0)/sharness.sh

# Each test launches its own `flux start` with a different --conf so
# each gets a fresh broker; no test_under_flux at the top of the file.
# The fake-resources task only activates when the [fake-resources]
# config table is present, so the no-config test verifies a default
# broker still works.

test_expect_success 'task does not activate without [fake-resources] config' '
	flux start flux resource list -no {nodelist} > nodelist.out &&
	! grep -q "fake" nodelist.out
'

test_expect_success 'minimal --conf with just nnodes succeeds' '
	flux start --conf=fake-resources.nnodes=4 true
'

test_expect_success 'nnodes produces requested node count' '
	flux start --conf=fake-resources.nnodes=4 \
		flux resource info > nnodes.out &&
	grep -q "4 Nodes" nnodes.out
'

test_expect_success 'cores-per-node honored' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.cores-per-node=8 \
		flux resource info > cores.out &&
	grep -q "32 Cores" cores.out
'

test_expect_success 'gpus-per-node honored' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.cores-per-node=2 \
		--conf=fake-resources.gpus-per-node=2 \
		flux resource info > gpus.out &&
	grep -q "8 GPUs" gpus.out
'

test_expect_success 'host-prefix honored' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.host-prefix=node \
		flux resource list -no {nodelist} > prefix.out &&
	grep -q "node" prefix.out &&
	! grep -q "fake" prefix.out
'

test_expect_success 'all options compose' '
	flux start \
		--conf=fake-resources.nnodes=4 \
		--conf=fake-resources.cores-per-node=8 \
		--conf=fake-resources.gpus-per-node=2 \
		--conf=fake-resources.host-prefix=node \
		flux resource info > combined.out &&
	grep -q "4 Nodes" combined.out &&
	grep -q "32 Cores" combined.out &&
	grep -q "8 GPUs" combined.out
'

test_expect_success 'sched-simple allocates jobs against fake R' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.cores-per-node=2 \
		flux submit --wait \
			--setattr=system.exec.test.run_duration=0.001s \
			-N 4 -n 4 true
'

test_expect_success 'missing nnodes fails broker start' '
	test_must_fail flux start \
		--conf=fake-resources.cores-per-node=8 true
'

# When hwloc-xml-path is set, fake-resources passes --xml=FILE to
# flux R encode for per-node topology. The rank/host flags are passed
# alongside so the configured nnodes replicates the per-node shape
# and the configured host-prefix overrides the Machine HostName in
# the XML. Tests use lstopo to capture the test runner's local
# topology — that gives us valid hwloc XML without trying to
# hand-write one (which flux R encode rejects with "Invalid
# argument"). Tests are skipped when lstopo isn't available.

test_expect_success 'capture local hwloc topology for hwloc-xml-path tests' '
	if command -v lstopo >/dev/null 2>&1; then
		lstopo --of xml local.xml &&
		test_set_prereq HAVE_LSTOPO
	elif command -v hwloc-ls >/dev/null 2>&1; then
		hwloc-ls --of xml local.xml &&
		test_set_prereq HAVE_LSTOPO
	fi
'

test_expect_success HAVE_LSTOPO \
	'hwloc-xml-path with nnodes=1 uses host-prefix not XML hostname' '
	flux start --conf=fake-resources.nnodes=1 \
		--conf=fake-resources.hwloc-xml-path=$(pwd)/local.xml \
		flux kvs get resource.R >r-1.json &&
	grep -q "fake0" r-1.json
'

test_expect_success HAVE_LSTOPO \
	'hwloc-xml-path with nnodes=4 replicates topology' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.hwloc-xml-path=$(pwd)/local.xml \
		flux kvs get resource.R >r-4.json &&
	grep -q "fake\\[0-3\\]" r-4.json
'

test_expect_success HAVE_LSTOPO \
	'hwloc-xml-path with custom host-prefix flows through' '
	flux start --conf=fake-resources.nnodes=4 \
		--conf=fake-resources.host-prefix=node \
		--conf=fake-resources.hwloc-xml-path=$(pwd)/local.xml \
		flux kvs get resource.R >r-hp.json &&
	grep -q "node\\[0-3\\]" r-hp.json &&
	test_must_fail grep -q "fake" r-hp.json
'

# The amend-r TOML key accepts two forms (resolved by
# flux.testing.fake_resources.load_amender):
#
#   * a path to a Python file that defines an ``amend(R, hwloc_xml=None)``
#     callable at module scope;
#   * a "module:function" reference imported via importlib.
#
# Both should produce a broker whose resource.R contains whatever
# amendments the callable made. We construct a marker amender and
# verify the marker key lands in the KVS post-startup.

test_expect_success 'amend-r as file path runs amender on R' '
	cat <<-EOT >amender_file.py &&
		def amend(R, hwloc_xml=None):
		    R["test_marker"] = "from-file"
		    return R
	EOT
	flux start --conf=fake-resources.nnodes=2 \
		--conf=fake-resources.amend-r=$(pwd)/amender_file.py \
		flux kvs get resource.R >file-R.out &&
	grep -q "from-file" file-R.out
'

test_expect_success 'amend-r as module:function runs amender on R' '
	mkdir -p amender_pkg &&
	cat <<-EOT >amender_pkg/__init__.py &&
		def amend(R, hwloc_xml=None):
		    R["test_marker"] = "from-module"
		    return R
	EOT
	PYTHONPATH="$(pwd):$PYTHONPATH" flux start \
		--conf=fake-resources.nnodes=2 \
		--conf=fake-resources.amend-r=amender_pkg:amend \
		flux kvs get resource.R >module-R.out &&
	grep -q "from-module" module-R.out
'

test_expect_success 'amend-r with missing file fails broker start' '
	test_must_fail flux start \
		--conf=fake-resources.nnodes=2 \
		--conf=fake-resources.amend-r=/no/such/amender.py \
		true
'

test_done
