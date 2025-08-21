#!/bin/sh

test_description='Test flux-R front-end command'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'flux R fails on invalid R objects' '
	test_expect_code 1 flux R decode </dev/null &&
	echo {} | test_expect_code 1 flux R decode &&
	echo "{\"version\": 1}" | test_expect_code 1 flux R decode &&
	echo "{\"version\": 1, \"execution\": {}}" \
	    | test_expect_code 1 flux R decode
'
test_expect_success 'flux R decode works with empty R_lite' '
	echo "{\"version\": 1, \"execution\": {\"R_lite\": {}}}" \
	    | flux R decode > R.empty &&
	test $(flux R decode --count core < R.empty) -eq 0
'
test_expect_success 'flux R encode with no args creates expected result' '
	flux R encode | flux R decode &&
	test $(flux R encode | flux R decode --count node) -eq 1 &&
	test $(flux R encode | flux R decode --count core) -eq 1 &&
	test $(flux R encode | flux R decode --count gpu) -eq 0 &&
	test "$(flux R encode | flux R decode --short)" = "rank0/core0"
'
test_expect_success 'flux R encode --ranks works' '
	test $(flux R encode --ranks 0-1 | flux R decode --count node) -eq 2 &&
	test $(flux R encode --ranks 0,2 | flux R decode --count node) -eq 2 &&
	test $(flux R encode --ranks 0,2 | flux R decode --count core) -eq 2 &&
	test $(flux R encode --ranks 0,2 | flux R decode --ranks) = "0,2"
'
test_expect_success 'flux R encode --cores works ' '
	test $(flux R encode --cores 0-1 | flux R decode --count node) -eq 1 &&
	test $(flux R encode --cores 0-1 | flux R decode --count core) -eq 2 &&
	test $(flux R encode -c 0-1 -r 0-1 | flux R decode -c core) -eq 4
'
test_expect_success 'flux R encode --gpus works' '
	test $(flux R encode --gpus 0 | flux R decode --count node) -eq 1 &&
	test $(flux R encode --gpus 0 | flux R decode --count gpu) -eq 1 &&
	test $(flux R encode --gpus 0 | flux R decode --count core) -eq 0 &&
	test $(flux R encode --cores 0-1 -g 0 | flux R decode --count core) -eq 2 &&
	test $(flux R encode --cores 0-1 -g 0 | flux R decode --count gpu) -eq 1
'
test_expect_success 'flux R encode --hosts works' '
	test $(flux R encode --hosts=foo[0-1] | flux R decode -c node) -eq 2 &&
	hosts=$(flux R encode --hosts=foo[0-1] | flux R decode --nodelist) &&
	test "$hosts" = "foo[0-1]"
'
test_expect_success 'flux R encode/decode --property works' '
	flux R encode --hosts=foo[0-1] --gpus 0 --cores 0-1  \
	    --property xx:0 \
	    --property yy:1 \
	    --property all > properties.json &&
	test $(flux R decode -c node <properties.json) -eq 2 &&
	test $(flux R decode -c node --properties all <properties.json) -eq 2 &&
	test $(flux R decode -c node --properties xx <properties.json) -eq 1 &&
	test $(flux R decode -c node --properties yy <properties.json) -eq 1 &&
	test $(flux R decode -c node --properties ^all <properties.json) -eq 0 &&
	test $(flux R decode -c node --properties all,yy <properties.json) -eq 1 &&
	test $(flux R decode --nodelist --properties xx <properties.json) = foo0 &&
	test $(flux R decode --nodelist --properties yy <properties.json) = foo1 &&
	test $(flux R decode --nodelist --properties ^yy <properties.json) = foo0
'
test_expect_success 'flux R encode --property fails with invalid rank' '
	test_must_fail flux R encode -r 0-3 -p xx:3-5 >property-fail.out 2>&1 &&
	test_debug "cat property-fail.out" &&
	grep "ranks 4-5 not found" property-fail.out
'
test_expect_success 'flux R encode --xml works' '
	flux R encode --xml=$SHARNESS_TEST_SRCDIR/hwloc-data/sierra.xml \
	    > R.sierra &&
	result=$(flux R decode --short < R.sierra) &&
	test_debug "echo encode XML = $result" &&
	test "$result" = "rank0/core[0-43],gpu[0-3]"
'
test_expect_success 'flux R encode --xml works with AMD RSMI gpus' '
	flux R encode --xml=$SHARNESS_TEST_SRCDIR/hwloc-data/corona.xml \
	    > R.corona &&
	result=$(flux R decode --short < R.corona) &&
	test_debug "echo encode XML = $result" &&
	test "$result" = "rank0/core[0-47],gpu[0-7]"
'
test_expect_success 'flux R decode --include works' '
	result=$(flux R encode -r 0-1023 | flux R decode --include 5-7 --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank[5-7]/core0"
'
test_expect_success 'flux R decode --exclude works' '
	result=$(flux R encode -r 1-10 | flux R decode --exclude 5-7 --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank[1-4,8-10]/core0"
'
test_expect_success 'flux R append fails if R sets intersect' '
	(flux R encode -r 0-1 && flux R encode -r 1-2) \
	    | test_must_fail flux R append  &&
	(flux R encode -r 0-1 -c 0-1 -g 0 && flux R encode -r 1 -c 2-3 -g 0) \
	    | test_must_fail flux R append
'
test_expect_success 'flux R append works' '
	result=$( (flux R encode -r 0-1 -c 0-1 && flux R encode -r 1-2 -c 2-3) \
	    | flux R append | flux R decode --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank0/core[0-1] rank1/core[0-3] rank2/core[2-3]"
'
test_expect_success 'flux R append works when only some nodes have gpus' '
	result=$( (flux R encode -r 0-1 -c 0-1 && \
	       flux R encode -r 2-3 -c 0-1 -g 0-1) \
	    | flux R append | flux R decode --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank[0-1]/core[0-1] rank[2-3]/core[0-1],gpu[0-1]"
'

test_expect_success 'flux R diff works' '
	result=$( (flux R encode -r 0-1 -c 0-1 && flux R encode -r 0-1 -c 0) \
	    | flux R diff | flux R decode --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank[0-1]/core1"
'
test_expect_success 'flux R intersect works' '
	result=$( (flux R encode -r 0-3 -c 0-1 && flux R encode -r 1-5 -c 0) \
	    | flux R intersect | flux R decode --short) &&
	test_debug "echo $result" &&
	test "$result" = "rank[1-3]/core0" &&
	result=$( (flux R encode -r 0-3 -c 0-1 && flux R encode -r 4-5 -c 0) \
	    | flux R intersect | flux R decode --short) &&
	test_debug "echo $result" &&
	test -z "$result"
'
test_expect_success 'flux R remap works' '
	result=$(flux R encode -r 5,99,1000 | flux R remap | flux R decode -s) &&
	test_debug "echo $result" &&
	test "$result" = "rank[0-2]/core0"
'
test_expect_success 'flux R rerank works' '
	result=$(flux R encode -r 0-3 -H foo[0-3] \
	    | flux R rerank foo[3,2,1,0] | flux R decode --nodelist) &&
	test_debug "echo reranked $result" &&
	test "$result" = "foo[3,2,1,0]"
'
test_expect_success 'flux R verify works' '
	result=$( (flux R encode -r 1-10 -c 0-3 && flux R encode -r 1 -c 0-3) \
	        | flux R verify 2>&1) &&
	test_debug "echo $result"
'
test_expect_success 'flux R verify fails with mismatched ranks' '
	result=$( (flux R encode -r 1-10 -c 0-3 && flux R encode -r 0 -c 0-3) \
	        | test_must_fail flux R verify 2>&1) &&
	test_debug "echo $result"
'
test_expect_success 'flux R verify fails with mismatched hosts' '
	result=$( (flux R encode -r 1 -c 0-3 -H foo1 \
	           && flux R encode -r 1 -c 0-3 -H foo12) \
	        | test_must_fail flux R verify 2>&1) &&
	test_debug "echo $result"
'
test_expect_success 'flux R verify fails with mismatched resources' '
	(flux R encode  -r 1 -c 0-3 && flux R encode -r 1 -c 0-2) \
	    | test_must_fail flux R verify
'
test_expect_success 'flux R verify reports extra resources' '
	(flux R encode  -r 1 -c 0-3 && flux R encode -r 1 -c 0-7 -g 1) \
	    | flux R verify
'
test_expect_success 'flux R set-property works' '
	flux R encode -r 0-1 -c 0-3 -H foo[0-1] | \
	    flux R set-property all | \
	    flux R set-property xx:0 yy:1 > setprop.json &&
	test $(flux R decode -c node <setprop.json) -eq 2 &&
	test $(flux R decode -c node --properties all <setprop.json) -eq 2 &&
	test $(flux R decode -c node --properties xx <setprop.json) -eq 1 &&
	test $(flux R decode -c node --properties yy <setprop.json) -eq 1 &&
	test $(flux R decode -c node --properties ^all <setprop.json) -eq 0 &&
	test $(flux R decode -c node --properties all,yy <setprop.json) -eq 1 &&
	test $(flux R decode --nodelist --properties xx <setprop.json) = foo0 &&
	test $(flux R decode --nodelist --properties yy <setprop.json) = foo1 &&
	test $(flux R decode --nodelist --properties ^yy <setprop.json) = foo0
'
test_expect_success 'flux R set-property fails with unknown ranks' '
	flux R encode -r 0-1 | test_must_fail flux R set-property foo:1-2
'
test_expect_success 'scheduling opaque key is preserved' '
	flux R encode -r 0-3 -c 0-3 -g 0 -H foo[0-3] \
	    | jq ".scheduling = 42" > R.orig &&
	flux R decode < R.orig | jq -e ".scheduling == 42" &&
	flux R decode --include 0 < R.orig | jq -e ".scheduling == 42" &&
	flux R decode --exclude 0 < R.orig | jq -e ".scheduling == 42"
'
test_expect_success 'scheduling opaque key is preserved with append' '
	(cat R.orig && flux R encode -r 4 ) | flux R append \
	    | flux R decode | jq -e ".scheduling == 42"
'
test_expect_success 'scheduling opaque key is preserved with remap' '
	flux R remap < R.orig | flux R decode | jq -e ".scheduling == 42"
'
test_expect_success 'scheduling opaque key is preserved with diff' '
	(cat R.orig && flux R encode -r 1 ) | flux R diff \
	    | jq -e ".scheduling == 42"
'
test_expect_success 'scheduling key is preserved with intersect' '
	(cat R.orig && flux R encode -r 1 -H foo1) | flux R intersect \
	    | jq -e ".scheduling == 42"
'
test_expect_success 'use of --local,--xml and --hosts is supported' '
	ncores=$(flux R encode --local | flux R decode --count cores) &&
	flux R encode --local --hosts=fluke[0-16] > R.hosts &&
	test_debug "flux R decode --short < R.hosts" &&
	count=$(flux R decode --count cores < R.hosts) &&
	test "$count" = "$ncores" &&
	hosts=$(flux R decode --nodelist < R.hosts) &&
	test_debug "echo got $hosts" &&
	test "$hosts" = "fluke[0-16]"
'
test_expect_success 'flux R parse-config works' '
	mkdir conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo"
	cores = "0-1"
	gpus = "0"
	EOF
	flux R parse-config conf > conf.json &&
	test_debug "flux R decode --short < conf.json" &&
	test $(flux R decode -c node < conf.json) -eq 1 &&
	test $(flux R decode -c core < conf.json) -eq 2 &&
	test $(flux R decode -c gpu < conf.json) -eq 1 &&
	test "$(flux R decode --nodelist < conf.json)" = "foo"
'
test_expect_success 'flux R parse-config works (multiple entries)' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-2]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo2"
	gpus = "0"
	[[resource.config]]
	hosts = "foo0"
	properties = ["login"]
	EOF
	flux R parse-config conf > conf2.json &&
	test_debug "flux R decode --short < conf2.json" &&
	test $(flux R decode -c node < conf2.json) -eq 3 &&
	test $(flux R decode -c core < conf2.json) -eq 6 &&
	test $(flux R decode -c gpu < conf2.json) -eq 1 &&
	test "$(flux R decode --nodelist < conf2.json)" = "foo[0-2]" &&
	test "$(flux R decode -p login --nodelist < conf2.json)" = "foo0"
'
test_expect_success 'flux R parse-config fails on invalid TOML' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config fails on when resource.config missing' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[resource]
	noverify = true
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects empty host list' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo"
	cores = "0-1"
	[[resource.config]]
	hosts = ""
	cores = "0-3"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects invalid host list' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[p]"
	cores = "0-1"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects invalid idset' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo0"
	cores = "0-"
	EOF
	test_must_fail flux R parse-config conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo0"
	gpus = "2,1"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects host with no resources' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo11"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects missing hosts entry' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	cores = "0-1"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects invalid entry' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo11"
	cores = "0-3"
	junk = 5
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects invalid property' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo1"
	properties = ["de^bug"]
	EOF
	test_must_fail flux R parse-config conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo1"
	properties = [1]
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config detects when properties not an array' '
	mkdir -p conf &&
	cat <<-EOF >conf/resource.toml &&
	[[resource.config]]
	hosts = "foo[0-10]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo11"
	cores = "0-3"
	properties = "foo"
	EOF
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config also works with resource.path' '
	flux R encode -r 0-1 >R.path &&
	cat <<-EOF >conf/resource.toml &&
	resource.path = "R.path"
	EOF
	flux R parse-config conf
'
test_expect_success 'flux R parse-config fails when resource.path = bad R' '
	echo "bad json" >R.path &&
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config fails when resource.path = missing R' '
	rm -f R.path &&
	test_must_fail flux R parse-config conf
'
test_expect_success 'flux R parse-config works (+scheduling)' '
	mkdir -p conf3 &&
	jq -n ".sched = true" > conf3/sched.json &&
	cat <<-EOF >conf3/resource.toml &&
	resource.scheduling = "$(pwd)/conf3/sched.json"
	[[resource.config]]
	hosts = "foo[0-2]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo2"
	gpus = "0"
	EOF
	flux R parse-config conf3 > conf+sched.json &&
	jq -e <conf+sched.json ".scheduling.sched == true"
'
test_expect_success 'flux R parse-config detects bad scheduling key' '
	mkdir -p conf4 &&
	echo foo > conf4/sched.json &&
	cat <<-EOF >conf4/resource.toml &&
	resource.scheduling = "$(pwd)/conf4/sched.json"
	[[resource.config]]
	hosts = "foo[0-2]"
	cores = "0-1"
	[[resource.config]]
	hosts = "foo2"
	gpus = "0"
	EOF
	test_must_fail flux R parse-config conf4
'
test_done
