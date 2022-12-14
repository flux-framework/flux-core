#!/bin/sh
#

test_description='Test that Flux can be lauched by PMIx-enabled launcher'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

ARGS="-o,-Sbroker.rc1_path=,-Sbroker.rc3_path="

# prterun (any version) works
find_prterun() {
	local path

	path=$(which prterun) 2>/dev/null || return 1
	version=$($path -V | head -1 | awk '{print $NF;}')
	echo "found $path version $version" >&2
	echo $path
}

# Use convenient sort(1) option to determine if semantic version $1 >= $2
version_gte() {
	test "$( (echo $1; echo $2) | sort --version-sort | tail -1)" = $1
}

# mpirun.openmpi >= 4.0.3 works
find_mpirun() {
	local path
	local version

	path=$(which mpirun.openmpi 2>/dev/null) || return 1
	version=$($path -V | head -1 | awk '{print $NF;}')
	version_gte $version 4.0.3 || return 1
	echo "found $path version $version" >&2
	echo $path
}

if ! flux version | grep -q +pmix-bootstrap; then
	skip_all='skipping: not configured with --enable-pmix-bootstrap'
	test_done
fi

if prterun=$(find_prterun) || prterun=$(find_mpirun); then
	test_set_prereq HAVE_PRTERUN
fi

test_expect_success 'flux pmi --method=pmix fails outside of PMIX environment' '
	test_must_fail flux pmi --method=pmix barrier
'
test_expect_success 'flux pmi falls through to singleton method' '
	flux pmi barrier
'
test_expect_success HAVE_PRTERUN 'flux pmi --method=pmix barrier works' '
	FLUX_PMI_DEBUG=1 $prterun --map-by :OVERSUBSCRIBE --n 4 \
	    flux pmi --method=pmix -v barrier
'
test_expect_success HAVE_PRTERUN 'flux pmi --method=pmix exchange works' '
	FLUX_PMI_DEBUG=1 $prterun --map-by :OVERSUBSCRIBE --n 4 \
	    flux pmi --method=pmix -v exchange
'
test_expect_success HAVE_PRTERUN 'prterun can launch Flux' '
	echo 4 >size.exp &&
	FLUX_PMI_DEBUG=1 $prterun --map-by :OVERSUBSCRIBE --n 4 \
	    flux start ${ARGS} flux getattr size >size.out &&
	test_cmp size.exp size.out
'

test_done
