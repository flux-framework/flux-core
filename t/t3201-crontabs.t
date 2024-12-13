#!/bin/sh
#

test_description='Test rc1 loading of crontab files'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'empty cron.directory works' '
	mkdir cron.d &&
	flux start -Scron.directory=cron.d true
'
test_expect_success 'non-existent cron.directory works' '
	flux start -Scron.directory=noexist true
'
test_expect_success 'cron.directory with subdirectory works' '
	rm -rf cron.d &&
	mkdir -p cron.d/subdir &&
	flux start -Scron.directory=cron.d true
'
test_expect_success 'cron.directory with non-crontab file fails' '
	rm -rf cron.d &&
	mkdir cron.d &&
	echo zzz >cron.d/badtab &&
	test_must_fail flux start -Scron.directory=cron.d \
		true 2>bad.err &&
	grep "could not load crontab" bad.err
'
test_expect_success 'cron.directory with good crontab files works' '
	rm -rf cron.d &&
	mkdir cron.d &&
	echo "10 * * * * true" >cron.d/goodtab &&
	echo "20 * * * * hostname" >cron.d/goodtab2 &&
	flux start -Scron.directory=cron.d flux cron list >list.out &&
	grep true list.out &&
	grep hostname list.out
'

test_done
