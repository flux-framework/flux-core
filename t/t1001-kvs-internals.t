#!/bin/sh
#

test_description='kvs internals tests in a flux session

These tests are deeper than basic KVS tests, testing to ensure
internal functionality of the KVS is working as designed.  These tests
will generally cover non-obvious issues/features that a general user
would be unaware of.
'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b

test_kvs_key() {
	flux kvs get "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

#
# pending request stats are 0 at start of tests
#

test_expect_success 'kvs: no pending requests at start of tests' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0
'

#
# large value test
#
test_expect_success 'kvs: kvs get/put large raw values works' '
	flux kvs unlink -Rf $DIR &&
	dd if=/dev/urandom bs=4096 count=1 >random.data &&
	flux kvs put --raw $DIR.data=- <random.data &&
	flux kvs get --raw $DIR.data >reread.data &&
	test_cmp random.data reread.data
'

#
# key normalization tests
#
test_expect_success 'kvs: put with leading path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put ......$DIR.a.b.c=42 &&
	test_kvs_key $DIR.a.b.c 42
'
test_expect_success 'kvs: put with trailing path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c........=43 &&
	test_kvs_key $DIR.a.b.c 43
'
test_expect_success 'kvs: put with extra embedded path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.....a....b...c=44 &&
	test_kvs_key $DIR.a.b.c 44
'
test_expect_success 'kvs: get with leading path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=42 &&
	test_kvs_key ......$DIR.a.b.c 42
'
test_expect_success 'kvs: get with trailing path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=43 &&
	test_kvs_key $DIR.a.b.c........ 43
'
test_expect_success 'kvs: get with extra embedded path separators works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a.b.c=44 &&
	test_kvs_key $DIR.....a....b...c 44
'

#
# zero-length value tests
#
test_expect_success 'kvs: zero-length value handled by put/get --raw' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get --raw $DIR.a >empty.output &&
	test_cmp /dev/null empty.output
'
test_expect_success 'kvs: zero-length value handled by get with no options' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get $DIR.a >empty2.output &&
	test_cmp /dev/null empty2.output
'
test_expect_success 'kvs: zero-length value handled by get --treeobj' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs get --treeobj $DIR.a
'
test_expect_success 'kvs: zero-length value is made by put with no options' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs get --raw $DIR.a >empty3.output &&
	test_cmp /dev/null empty3.output
'
test_expect_success 'kvs: zero-length value does not cause dir to fail' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs dir $DIR
'
test_expect_success 'kvs: zero-length value does not cause ls -FR to fail' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put --raw $DIR.a= &&
	flux kvs ls -FR $DIR
'
test_expect_success 'kvs: append to zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs put --append $DIR.a=abc &&
	printf "%s\n" "abc" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: append a zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=abc &&
	flux kvs put --append $DIR.a= &&
	printf "%s\n" "abc" >expected &&
	flux kvs get $DIR.a >output &&
	test_cmp output expected
'
test_expect_success 'kvs: append a zero length value to zero length value works' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a= &&
	flux kvs put --append $DIR.a= &&
	flux kvs get $DIR.a >empty.output &&
	test_cmp /dev/null empty.output
'

#
# ELOOP tests
#
test_expect_success 'kvs: link: error on link depth' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.a=1 &&
	flux kvs link $DIR.a $DIR.b &&
	flux kvs link $DIR.b $DIR.c &&
	flux kvs link $DIR.c $DIR.d &&
	flux kvs link $DIR.d $DIR.e &&
	flux kvs link $DIR.e $DIR.f &&
	flux kvs link $DIR.f $DIR.g &&
	flux kvs link $DIR.g $DIR.h &&
	flux kvs link $DIR.h $DIR.i &&
	flux kvs link $DIR.i $DIR.j &&
	flux kvs link $DIR.j $DIR.k &&
	flux kvs link $DIR.k $DIR.l &&
	test_must_fail flux kvs get $DIR.l
'
test_expect_success 'kvs: link: error on link depth, loop' '
	flux kvs unlink -Rf $DIR &&
	flux kvs link $DIR.link1 $DIR.link2 &&
	flux kvs link $DIR.link2 $DIR.link1 &&
	test_must_fail flux kvs get $DIR.link1
'

#
# kvs reads/writes of raw data to/from content store work
#

largeval="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
largevalhash="sha1-d8cc4fd0a57d0e0e96cdb3e74164f734c593ed65"

test_expect_success 'kvs: large put stores raw data into content store' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.largeval=$largeval &&
	flux kvs get --treeobj $DIR.largeval | grep -q \"valref\" &&
	flux kvs get --treeobj $DIR.largeval | grep -q ${largevalhash} &&
	flux content load ${largevalhash} | grep $largeval
'

test_expect_success 'kvs: valref that points to content store data can be read' '
	flux kvs unlink -Rf $DIR &&
	echo "$largeval" | flux content store &&
	flux kvs put --treeobj $DIR.largeval2="{\"data\":[\"${largevalhash}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get $DIR.largeval2 | grep $largeval
'

test_expect_success 'kvs: valref that points to zero size content store data can be read' '
	flux kvs unlink -Rf $DIR &&
	hashval=`flux content store </dev/null` &&
	flux kvs put --treeobj $DIR.empty="{\"data\":[\"${hashval}\"],\"type\":\"valref\",\"ver\":1}" &&
	test $(flux kvs get --raw $DIR.empty|wc -c) -eq 0
'

test_expect_success 'kvs: valref can point to other treeobjs' '
	flux kvs unlink -Rf $DIR &&
	flux kvs mkdir $DIR.a.b.c &&
	dirhash=`flux kvs get --treeobj $DIR.a.b.c | grep -E "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $DIR.value="{\"data\":[\"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.value | grep dir
'

#
# multi-blobref valrefs
#

test_expect_success 'kvs: multi blob-ref valref can be read' '
	flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.multival | grep "abcdefgh" &&
	test $(flux kvs get --raw $DIR.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on left, can be read' '
	flux kvs unlink -Rf $DIR &&
	hashval1=`flux content store < /dev/null` &&
	hashval2=`echo -n "abcd" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.multival | grep "abcd" &&
	test $(flux kvs get --raw $DIR.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref on right, can be read' '
	flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.multival | grep "abcd" &&
	test $(flux kvs get --raw $DIR.multival|wc -c) -eq 4
'

test_expect_success 'kvs: multi blob-ref valref with an empty blobref in middle, can be read' '
	flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	hashval2=`flux content store < /dev/null` &&
	hashval3=`echo -n "efgh" | flux content store` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${hashval2}\", \"${hashval3}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.multival | grep "abcdefgh" &&
	test $(flux kvs get --raw $DIR.multival|wc -c) -eq 8
'

test_expect_success 'kvs: multi blob-ref valref with a blobref pointing to a treeobj' '
	flux kvs unlink -Rf $DIR &&
	hashval1=`echo -n "abcd" | flux content store` &&
	flux kvs mkdir $DIR.a.b.c &&
	dirhash=`flux kvs get --treeobj $DIR.a.b.c | grep -E "sha1-[A-Za-z0-9]+" -o` &&
	flux kvs put --treeobj $DIR.multival="{\"data\":[\"${hashval1}\", \"${dirhash}\"],\"type\":\"valref\",\"ver\":1}" &&
	flux kvs get --raw $DIR.multival | grep dir
'

#
# invalid blobrefs don't hang
#

# create valref with illegal content store blob
# call flux kvs get $DIR.bad_X twice, to ensure first time cleaned up properly

badhash="sha1-0123456789012345678901234567890123456789"

test_expect_success 'kvs: invalid valref lookup wont hang' '
	flux kvs put --treeobj $DIR.bad_valref="{\"data\":[\"${badhash}\"],\"type\":\"valref\",\"ver\":1}" &&
	! flux kvs get $DIR.bad_valref &&
	! flux kvs get $DIR.bad_valref
'

test_expect_success 'kvs: invalid valref get --watch wont hang' '
	! flux kvs get --watch $DIR.bad_valref &&
	! flux kvs get --watch $DIR.bad_valref
'

test_expect_success 'kvs: invalid multi-blobref valref lookup wont hang' '
	flux kvs put --treeobj $DIR.bad_multi_valref="{\"data\":[\"${badhash}\", \"${badhash}\"],\"type\":\"valref\",\"ver\":1}" &&
	! flux kvs get $DIR.bad_multi_valref &&
	! flux kvs get $DIR.bad_multi_valref
'

test_expect_success 'kvs: invalid multi-blobref valref get --watch wont hang' '
	! flux kvs get --watch $DIR.bad_multi_valref &&
	! flux kvs get --watch $DIR.bad_multi_valref
'

test_expect_success 'kvs: invalid dirref lookup wont hang' '
	flux kvs put --treeobj $DIR.bad_dirref="{\"data\":[\"${badhash}\"],\"type\":\"dirref\",\"ver\":1}" &&
	! flux kvs get $DIR.bad_dirref.a &&
	! flux kvs get $DIR.bad_dirref.a
'

test_expect_success 'kvs: invalid dirref get --watch wont hang' '
	! flux kvs get --watch $DIR.bad_dirref.a &&
	! flux kvs get --watch $DIR.bad_dirref.a
'

test_expect_success 'kvs: invalid dirref write wont hang' '
	flux kvs put --treeobj $DIR.bad_dirref="{\"data\":[\"${badhash}\"],\"type\":\"dirref\",\"ver\":1}" &&
	! flux kvs put $DIR.bad_dirref.a=1 &&
	! flux kvs put $DIR.bad_dirref.b=1
'

# N.B. kvs test personality loads content module with blob-size-limit=1048576
MAXBLOB=1048576

test_expect_success "kvs: failure to store blob that exceeds max size does not hang" '
	dd if=/dev/zero count=$(($MAXBLOB/4096+1)) bs=4096 \
			skip=$(($MAXBLOB/4096)) >toobig_long 2>/dev/null &&
	test_must_fail flux kvs put -r $DIR.bad_toobig_long=- < toobig_long
'

#
# test synchronization based on commit sequence no.
#

test_expect_success 'kvs: put on rank 0, exists on all ranks' '
	flux kvs put $DIR.xxx=99 &&
	VERS=$(flux kvs version) &&
	flux exec -n sh -c "flux kvs wait ${VERS} && flux kvs get $DIR.xxx"
'

test_expect_success 'kvs: unlink on rank 0, does not exist all ranks' '
	flux kvs unlink -Rf $DIR.xxx &&
	VERS=$(flux kvs version) &&
	flux exec -n sh -c "flux kvs wait ${VERS} && ! flux kvs get $DIR.xxx"
'

#
# test pause / unpause
#

test_expect_success 'kvs: pause / unpause works' '
	${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause &&
	${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause
'

# cover invalid namespace cases
test_expect_success 'kvs: cover pause / unpause namespace invalid' '
	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=illegalnamespace &&
	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=illegalnamespace
'

#
# test causal consistency
#

# test strategy is to get treeobj / sequence from a write operation, then test
# that the change is noticed on other ranks.  When using the treeobj, pause
# setroot events b/c it shouldn't be necessary.

test_expect_success 'kvs: causal consistency (put)' '
	flux kvs unlink -Rf $DIR &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause" &&
	ATREF=$(flux kvs put -O $DIR.testA=1) &&
	VAL=$(flux exec -n -r 1 flux kvs get --at $ATREF $DIR.testA) &&
	test "$VAL" = "1" &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause" &&
	VERS=$(flux kvs put -s $DIR.testB=2) &&
	flux exec -n -r 2 flux kvs wait $VERS &&
	VAL=$(flux exec -n -r 2 flux kvs get $DIR.testB) &&
	test "$VAL" = "2" &&
	flux exec -n -r [1-2] sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause"
'

test_expect_success 'kvs: causal consistency (mkdir)' '
	flux kvs unlink -Rf $DIR &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause" &&
	ATREF=$(flux kvs mkdir -O $DIR.dirA) &&
	! flux exec -n -r 1 flux kvs get --at $ATREF $DIR.dirA > output 2>&1 &&
	grep "Is a directory" output &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause" &&
	VERS=$(flux kvs mkdir -s $DIR.dirB) &&
	flux exec -n -r 2 flux kvs wait $VERS &&
	! flux exec -n -r 2 flux kvs get $DIR.dirB > output 2>&1 &&
	grep "Is a directory" output
'

test_expect_success 'kvs: causal consistency (link)' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.fooA=3 &&
	flux kvs put $DIR.fooB=4 &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause" &&
	ATREF=$(flux kvs link -O $DIR.fooA $DIR.linkA) &&
	VAL=$(flux exec -n -r 1 flux kvs get --at $ATREF $DIR.linkA) &&
	test "$VAL" = "3" &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause" &&
	VERS=$(flux kvs link -s $DIR.fooB $DIR.linkB) &&
	flux exec -n -r 2 flux kvs wait $VERS &&
	VAL=$(flux exec -n -r 2 flux kvs get $DIR.linkB) &&
	test "$VAL" = "4"
'

test_expect_success 'kvs: causal consistency (unlink)' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.unlinkA=5 &&
	flux kvs put $DIR.unlinkB=6 &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause" &&
	ATREF=$(flux kvs unlink -O $DIR.unlinkA) &&
	! flux exec -n -r 1 flux kvs get --at $ATREF $DIR.unlinkA > output 2>&1 &&
	grep "No such file or directory" output &&
	flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause" &&
	VERS=$(flux kvs unlink -s $DIR.unlinkB) &&
	flux exec -n -r 2 flux kvs wait $VERS &&
	! flux exec -n -r 2 flux kvs get $DIR.unlinkB > output 2>&1 &&
	grep "No such file or directory" output
'

#
# test read-your-writes consistency
#

# test strategy is as follows
# - write an original value
# - pause setroot event processing on some rank X
# - write to a specific rank Y (Y != X)
# - change should be visible on rank Y, but not rank X
# - unpause setroot events on rank X
# - change should be visible on X & Y

test_expect_success 'kvs: read-your-writes consistency on primary namespace' '
	flux kvs unlink -Rf $DIR &&
	flux kvs put $DIR.test=1 &&
	VERS=$(flux kvs version) &&
	flux exec -n sh -c "flux kvs wait ${VERS}" &&
	flux exec -n -r 2 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause" &&
	flux exec -n -r 1 sh -c "flux kvs put $DIR.test=2" &&
	flux exec -n -r 1 sh -c "flux kvs get $DIR.test" > rank1-a.out &&
	flux exec -n -r 2 sh -c "flux kvs get $DIR.test" > rank2-a.out &&
	echo "1" > old.out &&
	echo "2" > new.out &&
	test_cmp rank1-a.out new.out &&
	test_cmp rank2-a.out old.out &&
	flux exec -n -r 2 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause" &&
	flux exec -n sh -c "flux kvs wait ${VERS}" &&
	flux exec -n -r 1 sh -c "flux kvs get $DIR.test" > rank1-b.out &&
	flux exec -n -r 2 sh -c "flux kvs get $DIR.test" > rank2-b.out &&
	test_cmp rank1-b.out new.out &&
	test_cmp rank2-b.out new.out
'

test_expect_success 'kvs: read-your-writes consistency on alt namespace' '
	flux kvs namespace create rywtestns &&
	flux kvs put --namespace=rywtestns $DIR.test=1 &&
	VERS=$(flux kvs version --namespace=rywtestns) &&
	flux exec -n sh -c "flux kvs wait --namespace=rywtestns ${VERS}" &&
	flux exec -n -r 2 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=rywtestns" &&
	flux exec -n -r 1 sh -c "flux kvs put --namespace=rywtestns $DIR.test=2" &&
	flux exec -n -r 1 sh -c "flux kvs get --namespace=rywtestns $DIR.test" > rank1-a.out &&
	flux exec -n -r 2 sh -c "flux kvs get --namespace=rywtestns $DIR.test" > rank2-a.out &&
	echo "1" > old.out &&
	echo "2" > new.out &&
	test_cmp rank1-a.out new.out &&
	test_cmp rank2-a.out old.out &&
	flux exec -n -r 2 sh -c "${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=rywtestns" &&
	flux exec -n sh -c "flux kvs wait --namespace=rywtestns ${VERS}" &&
	flux exec -n -r 1 sh -c "flux kvs get --namespace=rywtestns $DIR.test" > rank1-b.out &&
	flux exec -n -r 2 sh -c "flux kvs get --namespace=rywtestns $DIR.test" > rank2-b.out &&
	test_cmp rank1-b.out new.out &&
	test_cmp rank2-b.out new.out &&
	flux kvs namespace remove rywtestns
'

#
# test invalid lookup rpc
#

test_expect_success 'kvs: test invalid lookup rpc' '
	${FLUX_BUILD_DIR}/t/kvs/lookup_invalid a-key > lookup_invalid_output &&
	grep "flux_future_get: Protocol error" lookup_invalid_output
'

#
# ensure pending requests are the expected number
#

test_expect_success 'kvs: 0 pending requests at end of tests before module removal' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0 &&
	pendingcount1=$(flux exec -n -r 1 sh -c "flux module stats -p pending_requests kvs") &&
	test $pendingcount1 -eq 0
'

#
# transaction module stats
#

test_expect_success 'kvs: module stats returns reasonable transaction stats' '
	commitdata=$(flux module stats -p transaction-opcount.commit kvs) &&
	echo $commitdata | jq -e ".count > 0" &&
	echo $commitdata | jq -e ".min > 0" &&
	echo $commitdata | jq -e ".max > 0" &&
	echo $commitdata | jq -e ".mean > 0.0" &&
	echo $commitdata | jq -e ".stddev >= 0.0"
'

#
# test empty kvs txn works
#

# "flux kvs sync" implements sync via an empty KVS txn
test_expect_success 'kvs: test empty kvs txn works' '
	flux kvs sync
'

test_expect_success 'kvs: module commit stats min is updated' '
	commitdata=$(flux module stats -p transaction-opcount.commit kvs) &&
	echo $commitdata | jq -e ".min == 0"
'

#
# test ENOSYS on unfinished requests when unloading the KVS module
#
# N.B. do this last as we are unloading the kvs module
#

wait_versionwaiters() {
	num=$1
	i=0
	while [ "$(flux module stats --parse namespace.primary.#versionwaiters kvs 2> /dev/null)" != "${num}" ] \
	      && [ $i -lt ${KVS_WAIT_ITERS} ]
	do
		sleep 0.1
		i=$((i + 1))
	done
	return $(loophandlereturn $i)
}

# In order to test, wait for a version that will not happen
test_expect_success NO_CHAIN_LINT 'kvs: ENOSYS returned on unfinished requests on module unload' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	pendingcountexp=$(($pendingcount+1)) &&
	WAITCOUNT=$(flux module stats --parse namespace.primary.#versionwaiters kvs) &&
	WAITCOUNT=$(($WAITCOUNT+1))
	VERS=$(flux kvs version) &&
	VERSWAIT=$(($VERS+10))
	flux kvs wait ${VERSWAIT} 2> enosys.err &
	pid=$! &&
	wait_versionwaiters ${WAITCOUNT} &&
	newcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcountexp -eq $newcount &&
	flux module remove kvs &&
	! wait $pid &&
	grep "Function not implemented" enosys.err
'

test_done
