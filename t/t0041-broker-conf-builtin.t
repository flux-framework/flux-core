#!/bin/sh
#
# ci=asan

test_description='Test broker.conf-builtin RPC'

. `dirname $0`/sharness.sh

test_under_flux 1

broker_conf_builtin() {
    keys=$(printf '"%s",' "$@" | sed 's/,$//') &&
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":[$keys]}).get_str())"
}

broker_conf_builtin_eproto() {
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.conf-builtin\",{}).get_str())"
}

test_expect_success 'conf-builtin can fetch single key' '
	broker_conf_builtin confdir >confdir.out &&
	jq -r -e .values.confdir <confdir.out >confdir.val &&
	test -n "$(cat confdir.val)"
'

test_expect_success 'conf-builtin can fetch multiple keys' '
	broker_conf_builtin confdir python_path exec_path >multi.out &&
	jq < multi.out &&
	jq -e ".values | length == 3" < multi.out
'

test_expect_success 'conf-builtin values are non-empty strings' '
	broker_conf_builtin confdir >check.out &&
	test $(jq -r -e .values.confdir <check.out | wc -c) -gt 1
'

test_expect_success 'conf-builtin fails with invalid key' '
	test_must_fail broker_conf_builtin invalid_key_name 2>invalid.err &&
	grep "invalid_key_name" invalid.err
'

test_expect_success 'conf-builtin fails on first invalid key in batch' '
	test_must_fail broker_conf_builtin confdir invalid_key python_path 2>batch_invalid.err &&
	grep "invalid_key" batch_invalid.err
'

test_expect_success 'conf-builtin without keys field fails with EPROTO' '
	test_must_fail broker_conf_builtin_eproto 2>eproto.err &&
	grep "Protocol error" eproto.err
'

test_expect_success 'conf-builtin with keys as non-array type fails with EPROTO' '
	test_must_fail flux python -c "import flux; flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":42}).get()" 2>eproto2.err &&
	grep "Protocol error" eproto2.err
'

test_expect_success 'conf-builtin works for simulated guest' '
	( export FLUX_HANDLE_ROLEMASK=0x2; \
	  export FLUX_HANDLE_USERID=$(($(id -u)+1)); \
	  broker_conf_builtin confdir \
	) >guest.out &&
	jq -r -e .values.confdir <guest.out
'

test_expect_success 'conf-builtin returns expected well-known keys' '
	broker_conf_builtin \
		confdir \
		libexecdir \
		datadir \
		python_path \
		exec_path \
		module_path \
		shell_path >wellknown.out &&
	jq -e ".values | length == 7" < wellknown.out &&
	jq -e ".values.confdir" < wellknown.out &&
	jq -e ".values.libexecdir" < wellknown.out &&
	jq -e ".values.datadir" < wellknown.out &&
	jq -e ".values.python_path" < wellknown.out &&
	jq -e ".values.exec_path" < wellknown.out &&
	jq -e ".values.module_path" < wellknown.out &&
	jq -e ".values.shell_path" < wellknown.out
'

test_expect_success 'conf-builtin with empty keys array returns empty values' '
	flux python -c "import flux; print(flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":[]}).get_str())" >empty.out &&
	jq -e ".values | length == 0" < empty.out
'

test_expect_success 'conf-builtin fails with empty string key' '
	test_must_fail \
	  flux python -c "import flux; flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":[\"\"]}).get()" 2>empty_key.err &&
	grep "invalid key" empty_key.err
'

test_expect_success 'conf-builtin with keys as string fails with EPROTO' '
	test_must_fail flux python -c "import flux; flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":\"string\"}).get()" 2>string_keys.err &&
	grep "Protocol error" string_keys.err
'

test_expect_success 'conf-builtin silently skips non-string array elements' '
	flux python -c "import flux; print(flux.Flux().rpc(\"broker.conf-builtin\",{\"keys\":[\"confdir\", 42, None, \"python_path\"]}).get_str())" >mixed.out &&
	jq -e ".values | length == 2" < mixed.out &&
	jq -e ".values.confdir" < mixed.out &&
	jq -e ".values.python_path" < mixed.out
'

test_done
