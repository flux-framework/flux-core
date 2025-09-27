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
	test_must_fail grep "Checking integrity of" log1.out
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
	grep "Checking integrity of" log2.out
'
