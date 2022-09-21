#!/bin/sh

test_description='Test flux jobs command config file support'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

test_expect_success 'run a job for job listing purposes' '
	flux mini submit sleep 300
'
test_expect_success 'flux-jobs --format=help works' '
	flux jobs --format=help >help.out &&
	test_debug "cat help.out" &&
	grep "flux-jobs output formats:" help.out
'
test_expect_success 'flux-jobs --format=invalid fails' '
	test_must_fail flux jobs --format=invalid
'
#  We can't add configuration to ~/.config or /etc/xdg/flux, so
#   just test that XDG_CONFIG_HOME works.
#
test_expect_success 'Additional formats can be added to XDG_CONFIG_HOME' '
	mkdir -p dir/flux &&
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	[formats.myformat]
        description = "my format"
	format = "{id.words}"
	EOF
	XDG_CONFIG_HOME="$(pwd)/dir" flux jobs --format=help >myformat.out &&
	test_debug "cat myformat.out" &&
	grep "my format" myformat.out &&
	XDG_CONFIG_HOME="$(pwd)/dir" flux jobs --format=myformat
'
test_expect_success 'flux-jobs config can be yaml' '
	mkdir -p yaml/flux &&
	cat <<-EOF >yaml/flux/flux-jobs.yaml &&
	formats:
	  myformat:
	    description: My YAML Format
	    format: "{id.f58} {runtime}"
	EOF
	XDG_CONFIG_HOME="$(pwd)/yaml" flux jobs --format=help >yaml.out &&
	test_debug "cat yaml.out" &&
	grep "My YAML Format" yaml.out &&
	XDG_CONFIG_HOME="$(pwd)/yaml" flux jobs --format=myformat
'
test_expect_success 'flux-jobs config can be json' '
	mkdir -p json/flux &&
	cat <<-EOF >json/flux/flux-jobs.json &&
	{ "formats": { "myformat": { "description": "My JSON Format",
	    "format": "{id.words} {runtime}" } } }
	EOF
	XDG_CONFIG_HOME="$(pwd)/json" flux jobs --format=help >json.out &&
	test_debug "cat json.out" &&
	grep "My JSON Format" json.out &&
	XDG_CONFIG_HOME="$(pwd)/json" flux jobs --format=myformat
'

test_expect_success 'XDG_CONFIG_DIRS can be colon-separated paths' '
	mkdir -p dir2/flux &&
	cat <<-EOF >dir2/flux/flux-jobs.toml &&
	[formats.myformat]
        description = "my new format"
	format = "{id.words} {name}"
	EOF
	XDG_CONFIG_DIRS="$(pwd)/dir2:$(pwd)/dir" \
		flux jobs --format=help >myformat.out &&
	test_debug "cat myformat.out" &&
	grep "my new format" myformat.out &&
	XDG_CONFIG_DIRS="$(pwd)/dir:$(pwd)/dir2" \
		flux jobs --format=myformat
'
test_expect_success 'Invalid flux-jobs config file causes meaningful error' '
	mkdir -p dir/flux &&
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	[formats.myformat]
        description = "missing format"
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux jobs --format=help >missing.out 2>&1
	) &&
	grep "required key .* missing" missing.out
'
test_expect_success 'flux-jobs checks that formats is a mapping' '
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	formats = "yay"
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux jobs --format=help >notamapping.out 2>&1
	) &&
	grep "must be a mapping" notamapping.out
'
test_expect_success 'flux-jobs checks that formats.NAME is a mapping' '
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	formats.myformat = 42
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux jobs --format=help >notamapping2.out 2>&1
	) &&
	grep "must be a mapping" notamapping2.out
'
test_expect_success 'flux-jobs checks that formats.NAME.format is a string' '
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	[formats.myformat]
	format = 42
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux jobs --format=help >notastring.out 2>&1
	) &&
	grep "must be a string" notastring.out
'
test_expect_success 'Invalid flux-jobs config file causes meaningful error' '
	mkdir -p dir/flux &&
	cat <<-EOF >dir/flux/flux-jobs.toml &&
	[formats.myformat]
        description = "invalid toml"
	format = "{id.f58}
	EOF
	( export XDG_CONFIG_HOME="$(pwd)/dir" &&
	  test_must_fail flux jobs --format=help >badtoml.out 2>&1
	) &&
	grep "flux-jobs.toml" badtoml.out
'
test_done
