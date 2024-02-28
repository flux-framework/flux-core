#!/bin/sh
#
test_description='Test flux-shell hwloc.xmlfile'

. `dirname $0`/sharness.sh

test_under_flux 2 job
unset HWLOC_XMLFILE
unset FLUX_HWLOC_XMLFILE

test_expect_success 'shell: HWLOC_XMLFILE is not set for normal jobs' '
	test_must_fail flux run printenv HWLOC_XMLFILE
'
test_expect_success 'shell: -o hwloc.xmlfile sets HWLOC_XMLFILE' '
	flux run -o hwloc.xmlfile printenv HWLOC_XMLFILE
'
test_expect_success 'shell: -o hwloc.xmlfile unsets HWLOC_COMPONENTS' '
	test_must_fail flux run --env=HWLOC_COMPONENTS=x86 -o hwloc.xmlfile \
		printenv HWLOC_COMPONENTS
'
command -v hwloc-info >/dev/null && test_set_prereq HWLOC_INFO
test_expect_success HWLOC_INFO 'shell: -o hwloc.xmlfile HWLOC_XMLFILE is valid' '
	flux run -o hwloc.xmlfile sh -c "hwloc-info --input \$HWLOC_XMLFILE"
'
test_expect_success 'shell: -o hwloc.xmlfile sets HWLOC_XMLFILE per node' '
	flux run -N2 --label-io -o hwloc.xmlfile printenv HWLOC_XMLFILE \
		> xmlfile.out &&
	test_debug "cat xmlfile.out" &&
	sed -n "s/^0://p" xmlfile.out >path.0 &&
	sed -n "s/^1://p" xmlfile.out >path.1 &&
	test_must_fail test_cmp path.0 path.1
'
test_expect_success 'shell: bad hwloc.xmlfile value is an error' '
	test_must_fail flux run -o hwloc.xmlfile=foo printenv HWLOC_XMLFILE
'
test_done
