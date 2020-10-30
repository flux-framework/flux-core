#!/bin/sh
#

test_description='Test config file overlay bootstrap'

. `dirname $0`/sharness.sh

# Avoid loading unnecessary modules in back to back broker tests
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

#
# check config file parsing
#
test_expect_success 'flux broker works with no config' '
	flux broker ${ARGS} flux lsattr -v | grep config.path >noconfig.out &&
	egrep "^config.path.*-$" noconfig.out
'

test_expect_success 'flux broker works with empty config directory' '
	mkdir empty &&
	cat <<-EOT >empty.exp &&
	empty
	EOT
	flux broker ${ARGS} -c empty flux getattr config.path >empty.out &&
	test_cmp empty.exp empty.out
'

test_expect_success 'FLUX_CONF_DIR also works to specify config dir' '
	FLUX_CONF_DIR=empty flux broker ${ARGS} \
		      flux getattr config.path >empty2.out &&
	test_cmp empty.exp empty2.out
'

test_expect_success 'flux broker fails with specfied config directory missing' "
	test_must_fail flux broker ${ARGS} -c noexist /bin/true
"

test_expect_success 'broker fails with invalid TOML' '
	mkdir conf1 &&
	cat <<-EOT >conf1/bootstrap.toml &&
	[bootstrap]
	bad-toml
	EOT
	test_must_fail flux broker ${ARGS} -c conf1 /bin/true
'

#
# [bootstrap] tests
#

test_expect_success '[bootstrap] config with bad hosts array' '
	mkdir conf3 &&
	cat <<-EOT >conf3/bootstrap.toml &&
	[bootstrap]
	hosts = 42
	EOT
	test_must_fail flux broker ${ARGS} -c conf3 /bin/true
'

test_expect_success '[bootstrap] config with bad hosts array element' '
	mkdir conf4 &&
	cat <<-EOT >conf4/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    42
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf4 /bin/true
'

test_expect_success '[bootstrap] config with hostname not found' '
	mkdir conf5 &&
	cat <<-EOT >conf5/bootstrap.toml &&
	[bootstrap]
	default_bind = "ipc://@flux-testipc-1-0"
	default_connect = "ipc://@flux-testipc-1-0"
	hosts = [
	    { host = "matchnobody" },
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf5 /bin/true
'

test_expect_success '[bootstrap] hosts array can be missing' '
	mkdir conf6 &&
	cat <<-EOT >conf6/bootstrap.toml &&
	[bootstrap]
	EOT
	flux broker ${ARGS} -c conf6 flux lsattr -v >missing_attr.out &&
	grep "tbon.endpoint.*-$" missing_attr.out
'

test_expect_success '[bootstrap] hosts array can be empty' '
	mkdir conf7 &&
	cat <<-EOT >conf7/bootstrap.toml &&
	[bootstrap]
	hosts = [
	]
	EOT
	flux broker ${ARGS} -c conf7 flux lsattr -v >empty_attr.out &&
	grep "tbon.endpoint.*-$" empty_attr.out
'

# Usage: start_broker config hostname cmd ...
start_broker() {
	local dir=$1; shift
	local host=$1; shift
	FLUX_FAKE_HOSTNAME=$host flux broker ${ARGS} -c $dir "$@" &
}

test_expect_success 'start size=2 instance with ipc://' '
	mkdir conf8 &&
	cat <<-EOT >conf8/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host="fake0", bind="ipc:///tmp/test-ipc2-0", connect="ipc:///tmp/test-ipc2-0" },
	    { host="fake1" }
	]
	EOT
	cat <<-EOF >attrdump.sh &&
	#!/bin/sh
	flux getattr size
	flux getattr config.hostlist
	EOF
	chmod +x attrdump.sh &&
	start_broker conf8 fake0 ./attrdump.sh >ipc.out &&
        F0=$! &&
	start_broker conf8 fake1 &&
        F1=$! &&
	wait $F0 $F1 &&
	echo 2 >ipc.exp &&
	echo "fake[0-1]" >> ipc.exp &&
	test_cmp ipc.exp ipc.out
'

test_expect_success 'start size=4 instance with tcp://' '
	mkdir conf9 &&
	cat <<-EOT >conf9/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host="fake0", bind="tcp://127.0.0.1:5080", connect="tcp://127.0.0.1:5080" },
	    { host="fake1", bind="tcp://127.0.0.1:5081", connect="tcp://127.0.0.1:5081" },
	    { host="fake[2-3]" }
	]
	EOT
	start_broker conf9 fake0 ./attrdump.sh >tcp.out &&
        F0=$! &&
	start_broker conf9 fake1 &&
        F1=$! &&
	start_broker conf9 fake2 &&
        F2=$! &&
	start_broker conf9 fake3 &&
        F3=$! &&
	wait $F0 $F1 $F2 $F3 &&
	echo 4 >tcp.exp &&
	echo "fake[0-3]" >> tcp.exp &&
	test_cmp tcp.exp tcp.out
'


test_done
