#
#  Ensure flux is fscked when not shutdown cleanly
#

test_expect_success 'get the current time for journalctl --since' '
	date +"%F %T" > fscktime.out
	cat fscktime.out
'

test_expect_success 'restart flux' '
	sudo systemctl restart flux
'

wait_flux_back_up() {
	i=0
	while ! flux resource list > /dev/null 2>&1 \
	      && [ $i -lt 100 ]
	do
		sleep 1
		i=$((i + 1))
	done
	if [ "$i" -eq "100" ]
	then
		return 1
	fi
	return 0
}

test_expect_success 'wait for flux to finish starting up' '
	wait_flux_back_up
'

test_expect_success 'fsck does not run after clean shutdown' '
	sudo journalctl --since "$(cat fscktime.out)" | grep rc1 > log1.out &&
	test_must_fail grep "Checking integrity" log1.out
'

test_expect_success 'kill flux broker' '
	pid=`sudo systemctl show --property=MainPID --value flux` &&
	sudo kill -s 9 $pid
'

# system instance for flux will auto restart in about 30 seconds
test_expect_success 'wait for flux to finish starting up' '
	wait_flux_back_up
'

test_expect_success 'fsck runs after unclean shutdown' '
	sudo journalctl --since "$(cat fscktime.out)" | grep rc1 > log2.out &&
	grep "Checking integrity" log2.out &&
	test_must_fail grep "Total errors" log2.out
'

test_expect_success 'corrupt some data' '
	JOBID=$(flux submit --wait hostname) &&
	kvsdir=$(flux job id --to=kvs $JOBID)
	sudo flux kvs put ${kvsdir}.testdata=1 &&
	sudo flux kvs put --append ${kvsdir}.testdata=2 &&
	sudo flux kvs put --append ${kvsdir}.testdata=3 &&
	sudo flux kvs get --treeobj ${kvsdir}.testdata > testdata.treeobj &&
	cat testdata.treeobj | jq -c .data[1]=\"sha1-1234567890123456789012345678901234567890\" > testdata.bad &&
	sudo flux kvs put --treeobj ${kvsdir}.testdatabad="$(cat testdata.bad)" &&
	test_must_fail sudo flux kvs get ${kvsdir}.testdatabad
'

test_expect_success 'call sync to ensure we have checkpointed' '
	sudo flux kvs sync
'

test_expect_success 'kill flux broker' '
	pid=`sudo systemctl show --property=MainPID --value flux` &&
	sudo kill -s 9 $pid
'

wait_fsck_fail() {
	i=0
	while ! sudo journalctl --since "$(cat fscktime.out)" | grep "missing blobref" > /dev/null 2>&1 \
	      && [ $i -lt 100 ]
	do
		sleep 1
		i=$((i + 1))
	done
	if [ "$i" -eq "100" ]
	then
		return 1
	fi
	return 0
}

# system instance for flux will auto restart in about 30 seconds
test_expect_success 'wait for flux to restart and fail fsck' '
	wait_fsck_fail
'

wait_flux_stopped() {
	i=0
	while sudo systemctl status flux > /dev/null 2>&1 \
	      && [ $i -lt 100 ]
	do
		sleep 1
		i=$((i + 1))
	done
	if [ "$i" -eq "100" ]
	then
		return 1
	fi
	return 0
}

test_expect_success 'flux system instance is not running' '
	wait_flux_stopped &&
	test_must_fail sudo systemctl status flux
'

test_expect_success 'fsck repaired KVS corruption' '
	sudo journalctl --since "$(cat fscktime.out)" | grep rc1 > log3.out &&
	grep "Total errors: 1" log3.out &&
	grep "Total repairs: 1" log3.out
'

test_expect_success 'fsck moved bad data to lost+found' '
	sudo -u flux flux start --recovery "flux kvs get lost+found.${kvsdir}.testdatabad" > testdatabad.out
'

test_expect_success 'corrupted data fixed correctly' '
	echo "13" > testdatabad.exp &&
	test_cmp testdatabad.exp testdatabad.out
'

test_expect_success 'other job data moved to lost+found too' '
	sudo -u flux flux start --recovery "flux kvs get lost+found.${kvsdir}.eventlog"
'

test_expect_success 'restart flux' '
	sudo systemctl start flux
'

test_expect_success 'wait for flux to finish starting back up' '
	wait_flux_back_up
'
