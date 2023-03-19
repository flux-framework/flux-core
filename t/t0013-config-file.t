#!/bin/sh
#

test_description='Test config file overlay bootstrap'

. `dirname $0`/sharness.sh


# Avoid loading unnecessary modules in back to back broker tests
ARGS="-Sbroker.rc1_path= -Sbroker.rc3_path="

# This option is compiled out of flux if zeromq is too old
if flux broker ${ARGS} flux getattr tbon.tcp_user_timeout >/dev/null 2>&1; then
	test_set_prereq MAXRT
else
	test_set_prereq NOMAXRT
fi

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
test_expect_success 'flux broker works with empty config file' '
	touch null.toml &&
	cat <<-EOT >null.exp &&
	null.toml
	EOT
	flux broker ${ARGS} -c null.toml flux getattr config.path >null.out &&
	test_cmp null.exp null.out
'
test_expect_success 'flux broker works with empty object config file (json)' '
	cat <<-EOF >empty.json &&
	{}
	EOF
	cat <<-EOT >empty-json.exp &&
	empty.json
	EOT
	flux broker ${ARGS} -c empty.json \
		flux getattr config.path >empty-json.out &&
	test_cmp empty-json.exp empty-json.out
'
test_expect_success 'FLUX_CONF_DIR also works to specify config dir' '
	FLUX_CONF_DIR=empty flux broker ${ARGS} \
		      flux getattr config.path >empty2.out &&
	test_cmp empty.exp empty2.out
'

test_expect_success 'flux broker fails with specfied config directory missing' "
	test_must_fail flux broker ${ARGS} -c noexist true
"

test_expect_success 'broker fails with invalid TOML' '
	mkdir conf1 &&
	cat <<-EOT >conf1/bootstrap.toml &&
	[bootstrap]
	bad-toml
	EOT
	test_must_fail flux broker ${ARGS} -c conf1 true
'

test_expect_success 'broker fails with invalid TOML file' '
	cat <<-EOT >invalid.toml &&
	[bootstrap]
	bad-toml
	EOT
	test_must_fail flux broker ${ARGS} -c invalid.toml true
'
#
# [bootstrap] tests
#
test_expect_success 'generate curve certficate for configuration' '
	flux keygen testcert
'

test_expect_success '[bootstrap] config with bad hosts array' '
	mkdir conf3 &&
	cat <<-EOT >conf3/bootstrap.toml &&
	[bootstrap]
	hosts = 42
	EOT
	test_must_fail flux broker ${ARGS} -c conf3 true
'

test_expect_success '[bootstrap] config with bad hosts array element' '
	mkdir conf4 &&
	cat <<-EOT >conf4/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    42
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf4 true
'

test_expect_success '[bootstrap] config with hosts array element with extra key' '
	mkdir conf4a &&
	cat <<-EOT >conf4a/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host = "xyz", extrakey = 42 },
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf4a true
'

test_expect_success '[bootstrap] config with hosts array element missing host' '
	mkdir conf4b &&
	cat <<-EOT >conf4b/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { },
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf4b true
'

test_expect_success '[bootstrap] config with bad hostlist' '
	mkdir conf4c &&
	cat <<-EOT >conf4c/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host = "foo[0-254}" },
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf4c true
'

test_expect_success '[bootstrap] config with with unknown parent' '
	mkdir conf4d &&
	cat <<-EOT >conf4d/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host = "fake0", parent = "noparent" },
	]
	EOT
	test_must_fail flux start --test-size=1 --test-hosts=fake0 \
	    -o,-c conf4d true
'

test_expect_success '[bootstrap] config with with impossible parent' '
	mkdir conf4e &&
	cat <<-EOT >conf4e/bootstrap.toml &&
	[bootstrap]
	hosts = [
	    { host = "fake0", parent = "fake0" },
	]
	EOT
	test_must_fail flux start --test-size=1 --test-hosts=fake0 \
	    -o,-c conf4e true
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
	test_must_fail flux broker ${ARGS} -c conf5 true
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

test_expect_success 'create initial program for testing' '
	cat <<-EOT >attrdump.sh &&
	#!/bin/sh
	flux getattr size
	flux getattr hostlist
	EOT
	chmod +x attrdump.sh
'

test_expect_success 'start size=2 instance with ipc://' '
	BINDDIR=$(mktemp -d) &&
	test_when_finished "rm -rf $BINDIR" &&
	mkdir conf8 &&
	cat <<-EOT >conf8/bootstrap.toml &&
	[bootstrap]
	curve_cert = "testcert"
	[[bootstrap.hosts]]
	host = "fake0"
	bind = "ipc://${BINDDIR}/test-ipc2-0"
	connect = "ipc://${BINDDIR}/test-ipc2-0"
	[[bootstrap.hosts]]
	host = "fake1"
	EOT
	flux start -s2 --test-hosts=fake[0-1] \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		-o,--config-path=conf8 \
		./attrdump.sh >ipc.out &&
	cat <<-EXP >ipc.exp &&
	2
	fake[0-1]
	EXP
	test_cmp ipc.exp ipc.out
'

test_expect_success 'start size=3 instance with ipc:// and custom topology' '
	BINDDIR=$(mktemp -d) &&
	test_when_finished "rm -rf $BINDIR" &&
	mkdir conf8a &&
	cat <<-EOT >conf8a/bootstrap.toml &&
	[bootstrap]
	curve_cert = "testcert"
	[[bootstrap.hosts]]
	host = "fake0"
	bind = "ipc://${BINDDIR}/fake0"
	connect = "ipc://${BINDDIR}/fake0"
	[[bootstrap.hosts]]
	host = "fake1"
	bind = "ipc://${BINDDIR}/fake1"
	connect = "ipc://${BINDDIR}/fake1"
	[[bootstrap.hosts]]
	host = "fake2"
	parent = "fake1"
	EOT
	flux start --test-size=3 --test-hosts=fake[0-2] \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		-o,--config-path=conf8a \
		flux getattr tbon.maxlevel >conf8a.out &&
	echo 2 >conf8a.exp &&
	test_cmp conf8a.exp conf8a.out
'

getport() {
	flux python -c \
	'from socket import socket; s=socket(); s.bind(("", 0)); print(s.getsockname()[1])'
}

test_expect_success 'start size=4 instance with tcp://' '
	PORT1=$(getport) &&
	PORT2=$(getport) &&
	mkdir conf9 &&
	cat <<-EOT >conf9/bootstrap.toml &&
	[bootstrap]
	curve_cert = "testcert"
	[[bootstrap.hosts]]
	host = "fake0"
	bind = "tcp://127.0.0.1:$PORT1"
	connect = "tcp://127.0.0.1:$PORT1"
	[[bootstrap.hosts]]
	host = "fake1"
	bind = "tcp://127.0.0.1:$PORT2"
	connect ="tcp://127.0.0.1:$PORT2"
	[[bootstrap.hosts]]
	host = "fake[2-3]"
	EOT
	flux start -s4 --test-hosts=fake[0-3] \
		-o,-Sbroker.rc1_path=,-Sbroker.rc3_path= \
		-o,--config-path=conf9 \
		./attrdump.sh >tcp.out &&
	cat <<-EXP >tcp.exp &&
	4
	fake[0-3]
	EXP
	test_cmp tcp.exp tcp.out
'

test_expect_success '[bootstrap] curve_cert is required for size > 1' '
	mkdir conf10 &&
	cat <<-EOT >conf10/bootstrap.toml &&
	[bootstrap]
	default_bind = "ipc://@flux-testipc-1-0"
	default_connect = "ipc://@flux-testipc-1-0"
	hosts = [
	    { host = "foo1" },
	    { host = "foo2" }
	]
	EOT
	test_must_fail flux broker ${ARGS} -c conf10 true
'

test_expect_success '[bootstrap] curve_cert must exist' '
	mkdir conf11 &&
	cat <<-EOT >conf11/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf11/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf11 true
'

test_expect_success '[bootstrap] curve_cert file must contain valid cert' '
	mkdir conf12 &&
	flux keygen conf12/cert &&
	sed --in-place -e "s/key/nerp/g" conf12/cert &&
	cat <<-EOT >conf12/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf12/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf12 true
'

test_expect_success '[bootstrap] curve_cert file must be mode g-r' '
	mkdir conf13 &&
	flux keygen conf13/cert &&
	chmod g+r conf13/cert &&
	cat <<-EOT >conf13/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf13/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf13 true
'

test_expect_success '[bootstrap] curve_cert file must be mode o-r' '
	mkdir conf14 &&
	flux keygen conf14/cert &&
	chmod o+r conf14/cert &&
	cat <<-EOT >conf14/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf14/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf14 true
'

#
# [tbon] test
#

test_expect_success MAXRT 'tbon.tcp_user_timeout is 20s by default' '
	cat <<-EOT >maxrt.exp &&
	20s
	EOT
	flux broker ${ARGS} \
		flux getattr tbon.tcp_user_timeout >maxrt.out &&
	test_cmp maxrt.exp maxrt.out
'
test_expect_success MAXRT 'tbon.tcp_user_timeout can be configured' '
	mkdir conf15 &&
	cat <<-EOT2 >maxrt2.exp &&
	30s
	EOT2
	cat <<-EOT >conf15/tbon.toml &&
	[tbon]
	tcp_user_timeout = "30s"
	EOT
	flux broker ${ARGS} -c conf15 flux getattr tbon.tcp_user_timeout \
		>maxrt2.out &&
	test_cmp maxrt2.exp maxrt2.out
'
test_expect_success MAXRT 'tbon.tcp_user_timeout command line overrides config' '
	cat <<-EOT >maxrt3.exp &&
	1h
	EOT
	flux broker ${ARGS} -c conf15 \
		-Stbon.tcp_user_timeout=1h \
		flux getattr tbon.tcp_user_timeout >maxrt3.out &&
	test_cmp maxrt3.exp maxrt3.out
'
test_expect_success MAXRT 'tbon.tcp_user_timeout with bad FSD on command line fails' '
	test_must_fail flux broker ${ARGS} \
		-Stbon.tcp_user_timeout=zzz \
		true 2>badattr.err &&
	grep "Error parsing" badattr.err
'
test_expect_success MAXRT 'tbon.tcp_user_timeout with bad FSD configured fails' '
	mkdir conf16 &&
	cat <<-EOT >conf16/tbon.toml &&
	[tbon]
	tcp_user_timeout = 42
	EOT
	test_must_fail flux broker ${ARGS} -c conf16 \
		true 2>badconf.err &&
	grep "Config file error" badconf.err
'
test_expect_success NOMAXRT 'tbon.tcp_user_timeout config cannot be set with old zeromq' '
	mkdir conf17 &&
	cat <<-EOT >conf17/tbon.toml &&
	[tbon]
	tcp_user_timeout = "30s"
	EOT
	test_must_fail flux broker ${ARGS} -c conf17 \
		true 2>noconf.err &&
	grep "unsupported by this zeromq version" noconf.err
'
test_expect_success NOMAXRT 'tbon.tcp_user_timeout attr cannot be set with old zeromq' '
	test_must_fail flux broker ${ARGS} \
		-Stbon.tcp_user_timeout=30s \
		true 2>noattr.err &&
	grep "unsupported by this zeromq version" noattr.err
'

test_expect_success 'tbon.zmqdebug is zero by default' '
	cat <<-EOT >zmqdebug.exp &&
	0
	EOT
	flux broker ${ARGS} \
		flux getattr tbon.zmqdebug >zmqdebug.out &&
	test_cmp zmqdebug.exp zmqdebug.out
'
test_expect_success 'tbon.zmqdebug can be configured' '
	mkdir conf18 &&
	cat <<-EOT2 >zmqdebug2.exp &&
	1
	EOT2
	cat <<-EOT >conf18/tbon.toml &&
	[tbon]
	zmqdebug = 1
	EOT
	flux broker ${ARGS} -c conf18 flux getattr tbon.zmqdebug \
		>zmqdebug2.out &&
	test_cmp zmqdebug2.exp zmqdebug2.out
'
test_expect_success MAXRT 'tbon.zmqdebug with bad value on command line fails' '
	test_must_fail flux broker ${ARGS} \
		-Stbon.zmqdebug=zzz \
		true 2>zbadattr.err &&
	grep "value must be an integer" zbadattr.err
'
test_expect_success MAXRT 'tbon.zmqdebug configured with wrong type fails' '
	mkdir conf19 &&
	cat <<-EOT >conf19/tbon.toml &&
	[tbon]
	zmqdebug = "notint"
	EOT
	test_must_fail flux broker ${ARGS} -c conf19 \
		true 2>zbadconf.err &&
	grep "Expected integer" zbadconf.err
'
test_expect_success MAXRT 'tbon.zmqdebug configured with wrong type fails' '
	mkdir conf20 &&
	cat <<-EOT >conf20/tbon.toml &&
	[tbon]
	zmqdebug = "notint"
	EOT
	test_must_fail flux broker ${ARGS} -c conf20 \
		true 2>zbadconf.err &&
	grep "Expected integer" zbadconf.err
'
test_expect_success 'tbon.torpid_max, tbon.torpid_min can be configured' '
	mkdir conf21 &&
	cat <<-EOT >conf21/tbon.toml &&
	[tbon]
	torpid_max = "5s"
	torpid_min = "2s"
	EOT
	flux broker ${ARGS} -c conf21 \
		flux getattr tbon.torpid_min >torpid_min.out &&
	flux broker ${ARGS} -c conf21 \
		flux getattr tbon.torpid_max >torpid_max.out &&
	grep 2s torpid_min.out &&
	grep 5s torpid_max.out
'
test_expect_success 'tbon.torpid_max configured with wrong type fails' '
	mkdir conf22 &&
	cat <<-EOT >conf22/tbon.toml &&
	[tbon]
	torpid_max = 5
	EOT
	test_must_fail flux broker ${ARGS} -c conf22 \
		true 2>badtorpid.err &&
	grep "Expected string" badtorpid.err
'
test_expect_success 'tbon.topo with unknown scheme fails' '
	mkdir conf23 &&
	cat <<-EOT >conf23/tbon.toml &&
	[tbon]
	topo = "notascheme:42"
	EOT
	test_must_fail flux broker ${ARGS} -c conf23 \
		true 2>badscheme.err &&
	grep "unknown topology scheme" badscheme.err
'
test_expect_success 'tbon.topo is kary:2 by default' '
	echo "kary:2" >topo.exp &&
	flux broker ${ARGS} flux getattr tbon.topo >topo.out &&
	test_cmp topo.exp topo.out
'
test_expect_success 'tbon.topo can be changed by configuration' '
	mkdir conf24 &&
	cat <<-EOT >conf24/tbon.toml &&
	[tbon]
	topo = "kary:8"
	EOT
	echo "kary:8" >topo2.exp &&
	flux broker ${ARGS} -c conf24 \
		flux getattr tbon.topo >topo2.out &&
	test_cmp topo2.exp topo2.out
'
test_expect_success 'tbon.topo can be overridden on the command line' '
	echo "kary:16" >topo3.exp &&
	flux broker ${ARGS} -c conf24 -Stbon.topo=kary:16 \
		flux getattr tbon.topo >topo3.out &&
	test_cmp topo3.exp topo3.out
'
test_expect_success 'tbon.topo is custom when bootstrap is configured' '
	mkdir conf25 &&
	cat <<-EOT >conf25/bootstrap.toml &&
	[bootstrap]
	EOT
	echo "custom" >topo4.exp &&
	flux broker ${ARGS} -c conf25 \
		flux getattr tbon.topo >topo4.out &&
	test_cmp topo4.exp topo4.out
'


test_done
