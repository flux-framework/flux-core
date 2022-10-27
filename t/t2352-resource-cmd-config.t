#!/bin/sh

test_description='flux-resource config tests'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 1 job

for cmd in list status drain; do
    test_expect_success "flux-resource ${cmd} --format=invalid fails" '
	test_must_fail flux resource ${cmd} --format=invalid
    '
done

#  We can't add configuration to ~/.config or /etc/xdg/flux, so
#   just test that XDG_CONFIG_HOME works.
#
test_expect_success 'Create flux-resource.toml config file' '
        mkdir -p dir/flux &&
        cat <<-EOF >dir/flux/flux-resource.toml
	[status.formats.myformat]
	description = "my status format"
	format = "{nodelist}"
	[list.formats.myformat]
	description = "my list format"
	format = "{state:>8.8} {nodelist}"
	[drain.formats.myformat]
	description = "my drain format"
	format = "{state} {reason}"
	EOF
'

for cmd in list status drain; do
    test_expect_success "flux-resource ${cmd} accepts configured formats" '
        XDG_CONFIG_HOME="$(pwd)/dir" \
		flux resource ${cmd} --format=help > ${cmd}-help.out &&
	test_debug "cat ${cmd}-help.out" &&
	grep myformat ${cmd}-help.out &&
        XDG_CONFIG_HOME="$(pwd)/dir" \
		flux resource ${cmd} --format=get-config=myformat \
		>${cmd}-get-config.out &&
	test_debug "cat ${cmd}-get-config.out" &&
	grep ${cmd}\.formats\.myformat ${cmd}-get-config.out &&
        XDG_CONFIG_HOME="$(pwd)/dir" \
		flux resource ${cmd}  --format=myformat
    '
done

test_expect_success 'flux-resource invalid key in subtable raises error' '
        cat <<-EOF >dir/flux/flux-resource.toml &&
	list.formats.foo = "nothing"
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux resource list --format=help >invalidkey.out 2>&1
	) &&
	test_debug "cat invalidkey.out" &&
	grep "flux-resource.toml" invalidkey.out
'
test_expect_success 'flux-resource invalid config key raises error' '
        cat <<-EOF >dir/flux/flux-resource.toml &&
	invalid.formats.foo.format = "nothing"
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux resource list --format=help >invalidkey.out 2>&1
	) &&
	test_debug "cat invalidkey.out" &&
	grep "flux-resource.toml" invalidkey.out
'
test_done
