#!/bin/sh

test_description='Test broker.getenv and broker.setenv RPCs'

. `dirname $0`/sharness.sh

export BROKER_ENV_TEST=abcd
unset BROKER_ENV_TEST2

test_under_flux 1

umask 022

broker_getenv() {
    names=$(printf '"%s",' "$@" | sed 's/,$//') &&
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.getenv\",{\"names\":[$names]}).get_str())"
}

broker_getenv_eproto() {
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.getenv\",{}).get_str())"
}

broker_setenv() {
    flux python -c "import flux, json; flux.Flux().rpc(\"broker.setenv\",{\"env\":json.loads('$1')}).get()"
}

broker_setenv_eproto() {
    flux python -c "import flux; flux.Flux().rpc(\"broker.setenv\",{}).get()"
}

test_expect_success 'getenv is able to fetch test variable' '
	cat >value.exp <<-EOT &&
	abcd
	EOT
	broker_getenv BROKER_ENV_TEST >getenv.out &&
	jq -r -e env.BROKER_ENV_TEST <getenv.out >value.out &&
	test_cmp value.exp value.out
'

test_expect_success 'getenv is able to fetch multiple variables' '
	broker_getenv BROKER_ENV_TEST PATH >getenv-multi.out &&
	jq < getenv-multi.out &&
	jq -e ".env | length == 2" < getenv-multi.out
'

test_expect_success 'getenv returns an empty object when variable is not set' '
	broker_getenv BROKER_ENV_TEST2 >getenv2.out &&
	test $(jq -e ".env | length" <getenv2.out) -eq 0
'

test_expect_success 'malformed getenv request fails with EPROTO' '
	test_must_fail broker_getenv_eproto 2>eproto.err &&
	grep "Protocol error" eproto.err
'

test_expect_success 'getenv fails for simulated guest' '
	( export FLUX_HANDLE_ROLEMASK=0x2; \
	  export FLUX_HANDLE_USERID=$(($(id -u)+1)); \
	  test_must_fail broker_getenv BROKER_ENV_TEST \
	) 2>eperm.err &&
	grep "Request requires owner credentials" eperm.err
'

test_expect_success 'setenv can set a new variable' '
	broker_setenv "{\"BROKER_ENV_TEST2\": \"efgh\"}" &&
	broker_getenv BROKER_ENV_TEST2 >setenv1.out &&
	test $(jq -r -e .env.BROKER_ENV_TEST2 <setenv1.out) = "efgh"
'

test_expect_success 'setenv can overwrite an existing variable' '
	broker_setenv "{\"BROKER_ENV_TEST\": \"wxyz\"}" &&
	broker_getenv BROKER_ENV_TEST >setenv2.out &&
	test $(jq -r -e .env.BROKER_ENV_TEST <setenv2.out) = "wxyz"
'

test_expect_success 'setenv can set multiple variables at once' '
	broker_setenv "{\"BROKER_ENV_MULTI1\": \"foo\", \
	                \"BROKER_ENV_MULTI2\": \"bar\"}" &&
	broker_getenv BROKER_ENV_MULTI1 BROKER_ENV_MULTI2 >setenv_multi.out &&
	test $(jq -r -e .env.BROKER_ENV_MULTI1 <setenv_multi.out) = "foo" &&
	test $(jq -r -e .env.BROKER_ENV_MULTI2 <setenv_multi.out) = "bar"
'

test_expect_success 'setenv with null value unsets the variable' '
	broker_setenv "{\"BROKER_ENV_TEST2\": null}" &&
	broker_getenv BROKER_ENV_TEST2 >setenv_unset.out &&
	test $(jq -e ".env | length" <setenv_unset.out) -eq 0
'

test_expect_success 'setenv with null value is a no-op for unset variable' '
	broker_setenv "{\"BROKER_ENV_NOTSET\": null}" &&
	broker_getenv BROKER_ENV_NOTSET >setenv_unset2.out &&
	test $(jq -e ".env | length" <setenv_unset2.out) -eq 0
'

test_expect_success 'setenv can mix set and unset in one call' '
	broker_setenv "{\"BROKER_ENV_MULTI1\": null, \"BROKER_ENV_MULTI3\": \"baz\"}" &&
	broker_getenv BROKER_ENV_MULTI1 >setenv_mix1.out &&
	broker_getenv BROKER_ENV_MULTI3 >setenv_mix2.out &&
	test $(jq -e ".env | length" <setenv_mix1.out) -eq 0 &&
	test $(jq -r -e .env.BROKER_ENV_MULTI3 <setenv_mix2.out) = "baz"
'

test_expect_success 'malformed setenv request fails with EPROTO' '
	test_must_fail broker_setenv_eproto 2>setenv_eproto.err &&
	grep "Protocol error" setenv_eproto.err
'

test_expect_success 'setenv fails when env value is not a string or null' '
	test_must_fail broker_setenv "{\"BROKER_ENV_BAD\": 42}" 2>setenv_bad.err &&
	grep "Protocol error" setenv_bad.err
'

test_expect_success 'setenv fails when variable name contains =' '
	test_must_fail broker_setenv "{\"BAD=NAME\": \"val\"}" 2>setenv_badname.err &&
	grep "Protocol error" setenv_badname.err
'

test_expect_success 'setenv fails for empty variable name' '
	test_must_fail broker_setenv "{\"\": \"val\"}" 2>setenv_emptyname.err &&
	grep "Protocol error" setenv_emptyname.err
'

test_expect_success 'setenv validation failure leaves no variables set' '
	test_must_fail broker_setenv \
	    "{\"SETENV_ATOMIC\": \"ok\", \
	      \"BAD=NAME\": \"val\", \
	      \"SETENV_ATOMIC2\": \"ok\"}" \
	    2>setenv_atomic.err &&
	grep "Protocol error" setenv_atomic.err &&
	broker_getenv SETENV_ATOMIC SETENV_ATOMIC2 >atomic.json &&
	test $(jq -e ".env | length" <atomic.json) -eq 0
'

test_expect_success 'setenv fails for simulated guest' '
	( export FLUX_HANDLE_ROLEMASK=0x2; \
	  export FLUX_HANDLE_USERID=$(($(id -u)+1)); \
	  test_must_fail broker_setenv "{\"BROKER_ENV_TEST\": \"val\"}" \
	) 2>setenv_eperm.err &&
	grep "Request requires owner credentials" setenv_eperm.err
'

test_done
