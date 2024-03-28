#
#  Ensure flux start --recover works on the system instance
#

test_expect_success 'dump the last checkpoint' '
        sudo -u flux flux dump --checkpoint /tmp/dump.tar
'
test_expect_success 'flux start --recover fails when instance is running' '
	test_must_fail sudo -u flux flux start --recover /bin/true
'
test_expect_success 'stop the system instance' '
	sudo flux shutdown
'
test_expect_success 'flux start --recover works' '
	sudo -u flux flux start --recover /bin/true
'
test_expect_success 'flux start --recover works from dump file' '
	sudo -u flux flux start --recover=/tmp/dump.tar --sysconfig /bin/true
'
test_expect_success 'restart flux' '
        sudo systemctl start flux
'
get_uptime_state () {
	local state=$(flux uptime | cut -d' ' -f3) || state=unknown
	echo $state
}
test_expect_success 'wait for flux to reach run state' '
	while test $(get_uptime_state) != run; do \
	    sleep 1; \
	done
'
