#!/bin/sh

test_description='Test command line plugin interface'

. $(dirname $0)/sharness.sh

export FLUX_CLI_PLUGINPATH=${SHARNESS_TEST_SRCDIR}/cli-plugins/

test_under_flux 4 job

test_expect_success 'flux-alloc: base --help message is formatted correctly' '
  flux alloc --help 
'
## Drop the following when development is over -- this is checked by other tests
test_expect_success 'flux-alloc: a job that does not provide plugins can run' '
  flux alloc -N1 hostname
'
test_expect_success 'flux-alloc: job that calls -P has jobspec set properly' '
  flux alloc -P matchpolicy=firstnodex -N1 --dry-run > jobspec1.json &&
  test $(jq -r .attributes.system.fluxion_match_policy jobspec1.json) = "firstnodex"
'
test_expect_success 'flux-alloc: job with invalid key to -P is rejected' '
  test_must_fail flux alloc -P matchpolicy=junkpolicy -N1 echo hello 2> err.out &&
  grep "flux-alloc: ERROR: Invalid option" err.out
'
test_expect_success 'flux-alloc: job with preinit has config set accordingly' '
  flux alloc -P gpumode=TPX -N1 flux config get > config.json && 
  test $(jq -r .resource.rediscover config.json) = "true"
'
test_expect_success 'flux-alloc: multiple valid plugin options are accepted' '
	flux alloc -N1 -P gpumode=TPX -P matchpolicy=firstnodex \
		flux config get > config2.json &&
	test $(jq -r .resource.rediscover config2.json) = "true" && 
	test $(jq -r ".[\"sched-fluxion-resource\"][\"match-policy\"] == \"firstnodex\"" config2.json) = "true"
'
test_expect_success 'flux-alloc: job with invalid plugin key is rejected outright' '
  test_must_fail flux alloc -N1 -P junk -P notthis=one -N 1 echo hello 2> out2.err &&
  grep "flux-alloc: ERROR: Unsupported option" out2.err
'
test_expect_success 'flux-run: validate-only plugins are properly validated' '
  test_must_fail flux run -o signal=foo hostname 2> out3.err &&
  grep "expected int or mapping got <class" out3.err &&
  test_must_fail flux run -o signal.foo hostname 2> out4.err &&
  grep "unsupported keys" out4.err
'

test_done
