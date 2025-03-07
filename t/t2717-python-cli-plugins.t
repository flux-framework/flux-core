#!/bin/sh

test_description='Test command line plugin interface'

. $(dirname $0)/sharness.sh

test_under_flux 4 job

export FLUX_CLI_PLUGINPATH=${FLUX_BUILD_DIR}/t/cli-plugins/cli/plugins

test_expect_success 'flux-run: base --help message is formatted correctly' '
  flux run --help
'
test_expect_success 'flux-run: a job that does not provide plugins can run' '
  flux run hostname
'
test_expect_success 'flux-alloc: job that calls -P has jobspec set properly' '
  flux alloc -P match-policy=firstnodex -N1 --dry-run > jobspec1.json &&
  test $(jq -r .attributes.system.fluxion_match_policy jobspec1.json) = "firstnodex"
'
test_expect_success 'flux-alloc: job with invalid key to -P is rejected' '
  test_must_fail flux alloc -P match-policy=junkpolicy -N1 echo hello 2> err.out &&
  grep "Unsupported option" err.out
'
test_expect_success 'flux-alloc: job with preinit has config set accordingly' '
  flux alloc -P gpumode=TPX -N1 flux config get > config.json && 
  test $(jq -r .resource.rediscover config.json) = "true"
'
test_expect_success 'flux-run: job with invalid plugin key is rejected outright' '
  test_must_fail flux run -P junk -P notthis=one -N 1 echo hello 2> out2.err &&
  grep -q "flux-run: ERROR: Unsupported option" out2.err

'

## Test plan: can take multiple plugins, one plugin, or no plugins; 
## sets the jobspec correctly; sets the config correctly;
## rejects invalid keys from the command line; 
## rejects invalid values provided to valid keys;
## rejects invalid options submitted to python;

test_done
