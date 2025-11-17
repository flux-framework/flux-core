#!/bin/sh

test_description='Test content ENOSPC corner cases'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile --debug
. `dirname $0`/sharness.sh

if ! ls /test/tmpfs-1m; then
	skip_all='skipping ENOSPC tests, no small tmpfs directory mounted'
	test_done
fi

test_expect_success 'create script to fill statedir' '
	cat >fillstatedir.sh <<-EOT &&
	#!/bin/sh
	while true ; do
	      flux run echo 0123456789 > /dev/null 2>&1
	      if flux dmesg | grep -q "No space left on device"; then
		   break
	      fi
	done
	EOT
	chmod +x fillstatedir.sh
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'flux still operates with content-sqlite running out of space' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -Scontent.backing-module=content-sqlite \
	    -Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux run echo helloworld" > sql.out 2> sql.err &&
	test_debug "cat sql.out" &&
	grep -q "No space left on device" sql.out &&
	grep "helloworld" sql.out
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'flux still operates with content-files running out of space' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -Scontent.backing-module=content-files \
	    -Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux run echo helloworld" > files.out 2> files.err &&
	test_debug "cat files.out" &&
	grep -q "No space left on device" files.out &&
	grep "helloworld" files.out
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'content flush returns error on ENOSPC' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -Scontent.backing-module=content-sqlite \
	    -Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux content flush" > flush.out 2> flush.err &&
	test_debug "cat flush.out flush.err" &&
	grep -q "No space left on device" flush.out &&
	grep "content.flush: No space left on device" flush.err
'

# flux start will fail b/c rc3 will fail due to ENOSPC
test_expect_success 'kvs sync fails due to ENOSPC' '
	rm -rf /test/tmpfs-1m/* &&
	mkdir /test/tmpfs-1m/statedir &&
	test_must_fail flux start \
	    -o,-Scontent.backing-module=content-sqlite \
	    -o,-Sstatedir=/test/tmpfs-1m/statedir \
	    "./fillstatedir.sh; flux dmesg; flux kvs put --sync foo=1" > sync.out 2> sync.err &&
	test_debug "cat sync.out sync.err" &&
	grep -q "No space left on device" sync.out &&
	grep "flux_kvs_commit: No space left on device" sync.err
'

test_done
