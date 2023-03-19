#
#  Ensure flux start --recover works on the system instance
#

test_expect_success 'dump the last checkpoint' '
        sudo -u flux flux dump --checkpoint /tmp/dump.tar
'
test_expect_success 'flux start --recover fails when instance is running' '
	test_must_fail sudo -u flux flux start --recover true
'
test_expect_success 'stop the system instance' '
	sudo flux shutdown
'
test_expect_success 'flux start --recover works' '
	sudo -u flux flux start --recover true
'
test_expect_success 'flux start --recover works from dump file' '
	sudo -u flux flux start --recover=/tmp/dump.tar --sysconfig true
'
test_expect_success 'restart flux' '
        sudo systemctl start flux
'
