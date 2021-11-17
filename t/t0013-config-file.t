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
test_expect_success 'generate curve certficate for configuration' '
	flux keygen testcert
'

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

test_expect_success 'create initial program for testing' '
	cat <<-EOT >attrdump.sh &&
	#!/bin/sh
	flux getattr size
	flux getattr hostlist
	EOT
	chmod +x attrdump.sh
'

test_expect_success 'start size=2 instance with ipc://' '
	mkdir conf8 &&
	cat <<-EOT >conf8/bootstrap.toml &&
	[bootstrap]
	curve_cert = "testcert"
	hosts = [
	    { host="fake0", bind="ipc:///tmp/test-ipc2-0", connect="ipc:///tmp/test-ipc2-0" },
	    { host="fake1" }
	]
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

test_expect_success 'start size=4 instance with tcp://' '
	mkdir conf9 &&
	cat <<-EOT >conf9/bootstrap.toml &&
	[bootstrap]
	curve_cert = "testcert"
	hosts = [
	    { host="fake0", bind="tcp://127.0.0.1:5080", connect="tcp://127.0.0.1:5080" },
	    { host="fake1", bind="tcp://127.0.0.1:5081", connect="tcp://127.0.0.1:5081" },
	    { host="fake[2-3]" }
	]
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
	test_must_fail flux broker ${ARGS} -c conf10 /bin/true
'

test_expect_success '[bootstrap] curve_cert must exist' '
	mkdir conf11 &&
	cat <<-EOT >conf11/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf11/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf11 /bin/true
'

test_expect_success '[bootstrap] curve_cert file must contain valid cert' '
	mkdir conf12 &&
	flux keygen conf12/cert &&
	sed --in-place -e "s/key/nerp/g" conf12/cert &&
	cat <<-EOT >conf12/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf12/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf12 /bin/true
'

test_expect_success '[bootstrap] curve_cert file must be mode g-r' '
	mkdir conf13 &&
	flux keygen conf13/cert &&
	chmod g+r conf13/cert &&
	cat <<-EOT >conf13/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf13/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf13 /bin/true
'

test_expect_success '[bootstrap] curve_cert file must be mode o-r' '
	mkdir conf14 &&
	flux keygen conf14/cert &&
	chmod o+r conf14/cert &&
	cat <<-EOT >conf14/bootstrap.toml &&
	[bootstrap]
	curve_cert = "conf14/cert"
	EOT
	test_must_fail flux broker ${ARGS} -c conf14 /bin/true
'

test_done
