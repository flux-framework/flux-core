#!/bin/sh

test_description='Test flux-R front-end command'

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
test_expect_success 'flux R encode --xml works' '
    flux R encode --xml=$SHARNESS_TEST_SRCDIR/hwloc-data/sierra2/0.xml \
        > R.sierra2 &&
    result=$(flux R decode --short < R.sierra2) &&
    test_debug "echo encode XML = $result" &&
    test "$result" = "rank0/core[0-43],gpu[0-3]"
'
test_expect_success 'flux R encode --xml works with AMD RSMI gpus' '
    flux R encode --xml=$SHARNESS_TEST_SRCDIR/hwloc-data/corona/0.xml \
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
test_expect_success HAVE_JQ 'scheduling opaque key is preserved' '
    flux R encode -r 0-3 -c 0-3 -g 0 -H foo[0-3] \
        | jq ".scheduling = 42" > R.orig &&
    flux R decode < R.orig | jq -e ".scheduling == 42" &&
    flux R decode --include 0 < R.orig | jq -e ".scheduling == 42" &&
    flux R decode --exclude 0 < R.orig | jq -e ".scheduling == 42"
'
test_expect_success HAVE_JQ 'scheduling opaque key is preserved with append' '
    (cat R.orig && flux R encode -r 4 ) | flux R append \
        | flux R decode | jq -e ".scheduling == 42"
'
test_expect_success HAVE_JQ 'scheduling opaque key is preserved with remap' '
    flux R remap < R.orig | flux R decode | jq -e ".scheduling == 42"
'
test_expect_success HAVE_JQ 'scheduling opaque key is preserved with diff' '
    (cat R.orig && flux R encode -r 1 ) | flux R diff \
        | jq -e ".scheduling == 42"
'
test_expect_success HAVE_JQ 'scheduling key is preserved with intersect' '
    (cat R.orig && flux R encode -r 1 -H foo1) | flux R intersect \
        | jq -e ".scheduling == 42"
'
test_expect_success HAVE_JQ 'use of --local,--xml and --hosts is supported' '
    ncores=$(flux R encode --local | flux R decode --count cores) &&
    flux R encode --local --hosts=fluke[0-16] > R.hosts &&
    test_debug "flux R decode --short < R.hosts" &&
    count=$(flux R decode --count cores < R.hosts) &&
    test "$count" = "$ncores" &&
    hosts=$(flux R decode --nodelist < R.hosts) &&
    test_debug "echo got $hosts" &&
    test "$hosts" = "fluke[0-16]"
'
test_done
