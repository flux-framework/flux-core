#!/bin/sh

test_description='Test broker.getenv rpc'

. `dirname $0`/sharness.sh

export BROKER_ENV_TEST=abcd
unset BROKER_ENV_TEST2

test_under_flux 1

umask 022

broker_getenv() {
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.getenv\",{\"names\":[\"$1\"]}).get_str())"
}

broker_getenv_eproto() {
    flux python -c "import flux; print(flux.Flux().rpc(\"broker.getenv\",{}).get_str())"
}

test_expect_success 'getenv is able to fetch test variable' '
	cat >value.exp <<-EOT &&
	abcd
	EOT
	broker_getenv BROKER_ENV_TEST >getenv.out &&
	jq -r -e env.BROKER_ENV_TEST <getenv.out >value.out &&
	test_cmp value.exp value.out
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

test_done
