#!/bin/sh

test_description='Test command line plugin interface'

. $(dirname $0)/sharness.sh

export FLUX_CLI_PLUGINPATH=${SHARNESS_TEST_SRCDIR}/cli-plugins/

test_under_flux 4 job

test_expect_success 'flux-alloc: base --help message includes plugin options' '
  flux run --help | grep -e "Options provided by plugins:" -e "--amd-gpumode"
'
test_expect_success 'flux-alloc: a job that does not provide plugins can run' '
  flux alloc -N1 hostname
'
test_expect_success 'flux-alloc: job that calls a plugin has jobspec set properly' '
  flux alloc --ex-match-policy=firstnodex -N1 --dry-run > jobspec1.json &&
  test $(jq -r .attributes.system.fluxion_match_policy jobspec1.json) = "firstnodex"
'
test_expect_success 'flux-alloc: job with invalid key to plugin is rejected' '
  test_must_fail flux alloc --ex-match-policy=junkpolicy -N1 echo hello 2> err.out &&
  grep "flux-alloc: ERROR: Invalid option" err.out
'
test_expect_success 'flux-alloc: job with preinit has config set accordingly' '
  flux alloc --amd-gpumode=TPX -N1 flux config get > config.json && 
  test $(jq -r .resource.rediscover config.json) = "true"
'
test_expect_success 'flux-alloc: multiple valid plugin options are accepted' '
  flux alloc -N1 --amd-gpumode=TPX --ex-match-policy=firstnodex \
    flux config get > config2.json &&
  test $(jq -r .resource.rediscover config2.json) = "true" && 
  test $(jq -r ".[\"sched-fluxion-resource\"][\"match-policy\"] == \"firstnodex\"" config2.json) = "true"
'
test_expect_success 'flux-alloc: plugin without a prefix behaves appropriately' '
  test_must_fail flux alloc -N1 --multi-user hostname 2> err0.out &&
  grep "Can only use --multi-user within multi-user instance" err0.out
'
test_expect_success 'flux-alloc: job with invalid plugin key is rejected outright' '
  test_must_fail flux alloc -N1 --ex-junk --ex-notthis=one -N 1 echo hello 2> out2.err &&
  grep "flux alloc: error: unrecognized arguments" out2.err
'
test_expect_success 'flux-run: validate-only plugins are properly validated' '
  test_must_fail flux run -o signal=foo hostname 2> out3.err &&
  grep "expected int or mapping got <class" out3.err &&
  test_must_fail flux run -o signal.foo hostname 2> out4.err &&
  grep "unsupported keys" out4.err &&
  test_must_fail flux run -o verbose=blekh hostname 2> out5.err &&
  grep "shell.options.verbose: expected integer, got <class" out5.err
'
test_expect_success 'flux-alloc: new plugin with the same name/dest as another fails' '
  export FLUX_CLI_PLUGINPATH=${SHARNESS_TEST_SRCDIR}/cli-plugins/extras/failure &&
  test_must_fail flux run --help 2> out6.err &&
  grep "conflicts with another option" out6.err &&
  unset FLUX_CLI_PLUGINPATH
'
test_expect_success 'flux-alloc: new plugin with the same name, diff dest/prefix is accepted' '
  FLUX_CLI_PLUGINPATH=${SHARNESS_TEST_SRCDIR}/cli-plugins/extras/success flux run --help >> help1.out &&
  grep -e "I am a valid plugin" -e "Option for setting AMD SMI compute partitioning" help1.out
'

test_done
