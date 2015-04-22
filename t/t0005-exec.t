#!/bin/sh
#

test_description='Test broker exec functionality, used by later tests


Test exec functionality
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

test_expect_success 'basic exec functionality' '
	flux exec /bin/true
'

test_expect_success 'exec to specific rank' '
	flux exec -r 0 /bin/true
'

test_expect_success 'exec to non-existent rank is an error' '
	test_must_fail flux exec -r 9999 /bin/true
'

test_expect_success 'test_on_rank works' '
	test_on_rank 1 /bin/true
'

test_expect_success 'test_on_rank sends to correct rank' '
	flux comms info | grep rank=0 && 
	test_on_rank 1 sh -c "flux comms info | grep -q rank=1"
'

test_expect_success 'test_on_rank works with test_must_fail' '
	test_must_fail test_on_rank 1 sh -c "flux comms info | grep -q rank=0"
'

test_expect_success 'flux exec passes environment variables' '
	test_must_fail flux exec -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	FOOTEST=t &&
	export FOOTEST &&
	flux exec -r 0 sh -c "test \"\$FOOTEST\" = \"t\"" &&
	test_on_rank 0 sh -c "test \"\$FOOTEST\" = \"t\""
'

test_expect_success 'flux exec does not pass FLUX_TMPDIR' '
        # Ensure FLUX_TMPDIR for rank 1 doesn not equal FLUX_TMPDIR for 0
	flux exec -r 1 sh -c "test \"\$FLUX_TMPDIR\" != \"$FLUX_TMPDIR\""
'

test_expect_success 'flux exec passes cwd' '
	(cd /tmp &&
	flux exec sh -c "test \`pwd\` = \"/tmp\"")
'

test_expect_success 'flux exec -d option works' '
	flux exec -d /tmp sh -c "test \`pwd\` = \"/tmp\""
'

# Run a script on ranks 0-3 simultaneously with each rank writing the
#  rank id to a file. After successful completion, the contents of the files
#  are verfied to ensure each rank connected to the right broker.
test_expect_success 'test_on_rank works on multiple ranks' '
	ouput_dir=$(pwd) &&
	rm -f rank_output.* &&
	cat >multiple_rank_test <<EOF &&
rank=\`flux comms info | grep rank | sed s/rank=//\`
echo \$rank > $(pwd)/rank_output.\${rank}
exit 0
EOF
	test_on_rank 0-3 sh $(pwd)/multiple_rank_test &&
	test `cat rank_output.0`  = "0" &&
	test `cat rank_output.1`  = "1" &&
	test `cat rank_output.2`  = "2" &&
	test `cat rank_output.3`  = "3"
'



test_done
